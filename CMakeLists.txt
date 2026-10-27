cmake_minimum_required(VERSION 3.20)
project(TaskForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable compiler warnings
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core Domain Library (Zero external dependencies)
add_library(taskforge_core
    src/core/task_id.cpp
    src/core/task_definition.cpp
    src/core/workflow_definition.cpp
    src/core/dependency_graph.cpp
    src/core/validation_error.cpp
    src/core/workflow_validator.cpp
    src/core/task_state.cpp
    src/core/task_result.cpp
    src/core/workflow_execution.cpp
)

target_include_directories(taskforge_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Sequential Scheduler Library
add_library(taskforge_scheduler
    src/scheduler/process_executor.cpp
    src/scheduler/workflow_scheduler.cpp
)

target_include_directories(taskforge_scheduler
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(taskforge_scheduler
    PUBLIC
        taskforge_core
)

# Third-party Dependencies
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Manifest Parsing Library
add_library(taskforge_manifest
    src/manifest/manifest_parser.cpp
)

target_include_directories(taskforge_manifest
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(taskforge_manifest
    PUBLIC
        taskforge_core
    PRIVATE
        nlohmann_json::nlohmann_json
)

# CLI Application Library
add_library(taskforge_cli
    src/cli/cli_app.cpp
)

target_include_directories(taskforge_cli
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(taskforge_cli
    PUBLIC
        taskforge_manifest
)

# CLI Main Executable
add_executable(taskforge
    src/cli/main.cpp
)

target_link_libraries(taskforge
    PRIVATE
        taskforge_cli
)

if(WIN32)
    target_link_libraries(taskforge_manifest
        PRIVATE
            kernel32
    )
    target_link_libraries(taskforge
        PRIVATE
            shell32
            kernel32
    )
endif()

# Testing configuration
enable_testing()
add_subdirectory(tests)
