cmake_minimum_required(VERSION 3.20)

project(FileDeduplicator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(FileHasher
    src/FileHasher.cpp
)

target_link_libraries(FileHasher PRIVATE
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)

add_executable(DuplicateFinder
    src/DuplicateFinder.cpp
)

target_link_libraries(DuplicateFinder PRIVATE
    nlohmann_json::nlohmann_json
)
