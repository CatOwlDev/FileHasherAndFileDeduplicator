#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr std::size_t MAX_FILES = 1000;
constexpr std::size_t BUFFER_SIZE = 1024 * 1024;

// SHA-256: чтение файла блоками по 1 МБ.
std::string calculateSHA256(const fs::path& filePath)
{
    std::ifstream file(filePath, std::ios::binary);

    if (!file)
        throw std::runtime_error("Cannot open file: " + filePath.string());

    EVP_MD_CTX* context = EVP_MD_CTX_new();

    if (!context)
        throw std::runtime_error("Cannot create SHA256 context");

    if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1)
    {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("SHA256 initialization failed");
    }

    std::array<char, BUFFER_SIZE> buffer{};

    while (file)
    {
        file.read(buffer.data(), buffer.size());
        std::streamsize bytesRead = file.gcount();

        if (bytesRead > 0)
        {
            if (EVP_DigestUpdate(
                    context,
                    buffer.data(),
                    static_cast<std::size_t>(bytesRead)) != 1)
            {
                EVP_MD_CTX_free(context);
                throw std::runtime_error("SHA256 update failed");
            }
        }
    }

    if (file.bad())
    {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("File read error: " + filePath.string());
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLength = 0;

    if (EVP_DigestFinal_ex(context, digest, &digestLength) != 1)
    {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("SHA256 finalization failed");
    }

    EVP_MD_CTX_free(context);

    std::ostringstream result;

    for (unsigned int i = 0; i < digestLength; ++i)
    {
        result << std::hex
               << std::setw(2)
               << std::setfill('0')
               << static_cast<int>(digest[i]);
    }

    return result.str();
}

// Получаем обычные файлы из исходной папки.
std::vector<fs::path> getSourceFiles(const fs::path& source)
{
    std::vector<fs::path> files;

    for (const auto& entry : fs::directory_iterator(source))
    {
        if (entry.is_regular_file())
            files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end());

    return files;
}

// Сохраняем JSON через временный файл.
void saveJSON(const fs::path& destination, const json& data)
{
    fs::path temporary = destination;
    temporary += ".tmp";

    {
        std::ofstream output(temporary);

        if (!output)
            throw std::runtime_error("Cannot create JSON file");

        output << data.dump(4);

        if (!output)
            throw std::runtime_error("JSON write failed");
    }

    fs::rename(temporary, destination);
}

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2)
        {
            std::cerr << "Usage: FileHasher <path.txt>\n";
            return 1;
        }

        // Читаем путь к исходной папке.
        std::ifstream config(argv[1]);

        if (!config)
            throw std::runtime_error("Cannot open path.txt");

        std::string sourceString;
        std::getline(config, sourceString);

        if (sourceString.empty())
            throw std::runtime_error("Source path is empty");

        fs::path source = fs::absolute(sourceString);

        if (!fs::exists(source) || !fs::is_directory(source))
            throw std::runtime_error("Invalid source directory");

        // Рабочая директория.
        fs::path workspace = source / ".dedup_workspace";
        fs::path metadata = workspace / "metadata";

        fs::create_directories(metadata);

        std::size_t batchNumber = 1;

        while (true)
        {
            auto files = getSourceFiles(source);

            if (files.empty())
            {
                std::cout << "\nAll files processed.\n";
                break;
            }

            std::size_t count = std::min(files.size(), MAX_FILES);

            std::ostringstream batchName;
            batchName << "Frame " << batchNumber;

            fs::path batchDirectory = workspace / batchName.str();

            fs::path metadataFile =
                metadata / ("Frame_" + std::to_string(batchNumber) + ".json");

            // Не перезаписываем предыдущие результаты.
            if (fs::exists(batchDirectory) || fs::exists(metadataFile))
                throw std::runtime_error(
                    "Batch already exists: " + batchName.str());

            fs::create_directories(batchDirectory);

            // Хеш -> список относительных путей.
            std::map<std::string, std::vector<std::string>> hashTable;

            std::cout << "\nProcessing " << batchName.str()
                      << " (" << count << " files)\n";

            std::size_t processed = 0;

            for (std::size_t i = 0; i < count; ++i)
            {
                const fs::path& original = files[i];

                std::ostringstream numberedName;

                numberedName
                    << std::setw(6)
                    << std::setfill('0')
                    << (i + 1)
                    << original.extension().string();

                fs::path destination =
                    batchDirectory / numberedName.str();

                try
                {
                    // Перемещаем и переименовываем.
                    fs::rename(original, destination);

                    // Вычисляем SHA-256.
                    std::string hash = calculateSHA256(destination);

                    // Сохраняем относительный путь.
                    fs::path relative =
                        fs::relative(destination, workspace);

                    hashTable[hash].push_back(relative.generic_string());

                    ++processed;

                    std::cout << "\rProcessed: "
                              << processed << "/" << count
                              << std::flush;
                }
                catch (const std::exception& e)
                {
                    std::cerr << "\nError: " << e.what() << '\n';
                }
            }

            std::cout << '\n';

            if (processed == 0)
            {
                throw std::runtime_error(
                    "No files were successfully processed. "
                    "Check the batch directory before restarting.");
            }

            // Формируем JSON.
            json result;

            result["version"] = 1;
            result["batch"] = batchName.str();
            result["files"] = json::object();

            for (const auto& [hash, paths] : hashTable)
                result["files"][hash] = paths;

            // Сохраняем структуру на диск.
            saveJSON(metadataFile, result);

            std::cout << "Saved: "
                      << metadataFile << '\n';

            // Очищаем таблицу перед следующей партией.
            hashTable.clear();
            hashTable = {};

            ++batchNumber;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "\nFATAL ERROR: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

