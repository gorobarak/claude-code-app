cmake_minimum_required(VERSION 3.13)

project(codecrafters-claude-code)

set(CMAKE_CXX_STANDARD 23) # Enable the C++23 standard

file(GLOB_RECURSE SOURCE_FILES src/*.cpp src/*.hpp)
list(REMOVE_ITEM SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/playground.cpp)

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(claude-code ${SOURCE_FILES})

target_link_libraries(claude-code PRIVATE cpr::cpr nlohmann_json::nlohmann_json)

# Absolute path so tool schemas resolve regardless of the process working directory.
target_compile_definitions(claude-code PRIVATE TOOLS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools")

# Local-only scratchpad; gitignored, so absent on a fresh clone.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/playground.cpp)
    add_executable(playground EXCLUDE_FROM_ALL src/playground.cpp)
    target_link_libraries(playground PRIVATE cpr::cpr nlohmann_json::nlohmann_json)
endif()