include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.0
)
FetchContent_MakeAvailable(Catch2)

set(TEST_SOURCES
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

if(WIN32)
    list(APPEND TEST_SOURCES
        test_windows_process_executor.cpp
    )

    add_executable(taskforge_test_helper
        helpers/test_helper_main.cpp
    )

    target_link_libraries(taskforge_test_helper
        PRIVATE
            shell32
            kernel32
    )

    if(MINGW)
        target_compile_options(taskforge_test_helper PRIVATE -municode)
        target_link_options(taskforge_test_helper PRIVATE -municode)
    endif()
endif()

add_executable(taskforge_tests
    ${TEST_SOURCES}
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

if(WIN32)
    target_link_libraries(taskforge_tests
        PRIVATE
            taskforge_platform
    )

    target_compile_definitions(taskforge_tests
        PRIVATE
            TASKFORGE_TEST_HELPER_PATH="$<TARGET_FILE:taskforge_test_helper>"
    )

    add_dependencies(taskforge_tests taskforge_test_helper)
endif()

target_compile_definitions(taskforge_tests
    PRIVATE
        TASKFORGE_EXE_PATH="$<TARGET_FILE:taskforge>"
)

add_dependencies(taskforge_tests taskforge)

include(CTest)
include(Catch)
catch_discover_tests(taskforge_tests)
