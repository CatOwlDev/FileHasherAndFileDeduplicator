#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

using DuplicateTable =
    std::unordered_map<std::string, std::vector<fs::path>>;

// Проверяем относительный путь на попытку выхода за workspace.
bool isSafeRelativePath(const fs::path& path)
{
    if (path.empty() || path.is_absolute())
        return false;

    for (const auto& component : path)
    {
        if (component == "..")
            return false;
    }

    return true;
}

// Получаем JSON-файлы из metadata.
std::vector<fs::path> getMetadataFiles(const fs::path& metadata)
{
    std::vector<fs::path> files;

    for (const auto& entry : fs::directory_iterator(metadata))
    {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".json")
        {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());

    return files;
}

// Читаем одну структуру и добавляем её в общую таблицу.
void loadMetadata(
    const fs::path& metadataFile,
    const fs::path& workspace,
    DuplicateTable& table)
{
    std::ifstream input(metadataFile);

    if (!input)
        throw std::runtime_error(
            "Cannot open: " + metadataFile.string());

    json data;
    input >> data;

    if (data.value("version", 0) != 1)
        throw std::runtime_error("Unsupported JSON version");

    if (!data.contains("files") || !data["files"].is_object())
        throw std::runtime_error("Invalid files structure");

    for (auto it = data["files"].begin();
         it != data["files"].end();
         ++it)
    {
        const std::string& hash = it.key();

        if (!it.value().is_array())
            throw std::runtime_error("Invalid path array");

        for (const auto& item : it.value())
        {
            if (!item.is_string())
                throw std::runtime_error("Invalid path value");

            fs::path relative = item.get<std::string>();

            if (!isSafeRelativePath(relative))
                throw std::runtime_error("Unsafe relative path");

            fs::path absolute = workspace / relative;

            table[hash].push_back(absolute);
        }
    }

    std::cout << "Loaded: "
              << metadataFile.filename() << '\n';
}

// Перемещаем файлы одной группы.
void moveGroup(
    const std::vector<fs::path>& files,
    const fs::path& resultDirectory,
    std::size_t groupNumber)
{
    std::ostringstream folderName;

    folderName << "Group_"
               << std::setw(6)
               << std::setfill('0')
               << groupNumber;

    fs::path groupDirectory =
        resultDirectory / folderName.str();

    fs::create_directories(groupDirectory);

    std::size_t fileNumber = 1;

    for (const auto& source : files)
    {
        try
        {
            if (!fs::exists(source) || !fs::is_regular_file(source))
            {
                std::cerr << "Missing file: "
                          << source << '\n';
                continue;
            }

            std::ostringstream newName;

            newName << std::setw(6)
                    << std::setfill('0')
                    << fileNumber++
                    << source.extension().string();

            fs::path destination =
                groupDirectory / newName.str();

            fs::rename(source, destination);

            std::cout << "Moved: "
                      << source.filename()
                      << " -> "
                      << groupDirectory.filename()
                      << '\n';
        }
        catch (const fs::filesystem_error& e)
        {
            std::cerr << "Move error: "
                      << e.what() << '\n';
        }
    }
}

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2)
        {
            std::cerr
                << "Usage: DuplicateFinder <workspace>\n";
            return 1;
        }

        fs::path workspace = fs::absolute(argv[1]);

        if (!fs::exists(workspace) ||
            !fs::is_directory(workspace))
        {
            throw std::runtime_error("Invalid workspace");
        }

        fs::path metadata = workspace / "metadata";

        if (!fs::exists(metadata) ||
            !fs::is_directory(metadata))
        {
            throw std::runtime_error("Metadata directory not found");
        }

        fs::path resultDirectory = workspace / "Result";

        // Не допускаем случайной перезаписи старого результата.
        if (fs::exists(resultDirectory))
        {
            throw std::runtime_error(
                "Result directory already exists. "
                "Rename or remove it before running again.");
        }

        auto metadataFiles = getMetadataFiles(metadata);

        if (metadataFiles.empty())
            throw std::runtime_error("No metadata files found");

        // Общая таблица всех хешей.
        DuplicateTable duplicateTable;

        std::cout << "Loading metadata...\n";

        for (const auto& file : metadataFiles)
        {
            loadMetadata(file, workspace, duplicateTable);
        }

        std::cout << "\nTotal unique hashes: "
                  << duplicateTable.size() << '\n';

        // Считаем количество групп с дубликатами.
        std::size_t duplicateGroups = 0;
        std::size_t uniqueGroups = 0;

        for (const auto& [hash, paths] : duplicateTable)
        {
            if (paths.size() > 1)
                ++duplicateGroups;
            else
                ++uniqueGroups;
        }

        std::cout << "Duplicate groups: "
                  << duplicateGroups << '\n';

        std::cout << "Unique groups: "
                  << uniqueGroups << '\n';

        fs::create_directories(resultDirectory);

        std::cout << "\nMoving files...\n";

        // Перемещаем каждую группу в отдельную папку.
        std::size_t groupNumber = 1;

        for (const auto& [hash, paths] : duplicateTable)
        {
            moveGroup(
                paths,
                resultDirectory,
                groupNumber
            );

            ++groupNumber;
        }

        std::cout << "\nFinished!\n";

        std::cout << "Result directory: "
                  << resultDirectory << '\n';

        // Освобождаем память.
        duplicateTable.clear();

    }
    catch (const std::exception& e)
    {
        std::cerr << "\nFATAL ERROR: "
                  << e.what() << '\n';

        return 1;
    }

    return 0;
}

