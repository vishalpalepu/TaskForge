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
)

target_link_libraries(taskforge_tests
    PRIVATE
        taskforge_core
        Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(taskforge_tests)
