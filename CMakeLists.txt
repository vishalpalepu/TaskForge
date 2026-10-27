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
)

target_include_directories(taskforge_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Testing configuration
enable_testing()
add_subdirectory(tests)
