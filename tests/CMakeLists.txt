include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(taskforge_tests
    test_task_id.cpp
    test_task_definition.cpp
    test_workflow_definition.cpp
    test_dependency_graph.cpp
    test_workflow_validator.cpp
    test_task_state.cpp
    test_workflow_execution.cpp
    test_workflow_scheduler.cpp
    test_manifest_parser.cpp
    test_cli_app.cpp
)

target_include_directories(taskforge_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(taskforge_tests
    PRIVATE
        taskforge_core
        taskforge_scheduler
        taskforge_manifest
        taskforge_cli
        Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(taskforge_tests)
