#include "taskforge/core/task_definition.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace taskforge::core;

TEST_CASE("TaskDefinition construction and invariants", "[domain][task_definition]") {
    SECTION("Valid task definition is created correctly") {
        auto task = TaskDefinition::create(
            TaskId("compile"),
            "Compile the main executable",
            "g++.exe",
            {"-std=c++20", "-c", "main.cpp"},
            std::filesystem::path("src/"),
            {TaskId("generate-headers")},
            42);

        CHECK(task.id() == TaskId("compile"));
        CHECK(task.description() == "Compile the main executable");
        CHECK(task.executable() == "g++.exe");
        CHECK(task.arguments().size() == 3);
        REQUIRE(task.working_directory().has_value());
        CHECK(task.working_directory().value() == "src/");
        CHECK(task.dependencies().size() == 1);
        CHECK(task.dependencies()[0] == TaskId("generate-headers"));
        CHECK(task.declaration_index() == 42);
        CHECK(task.depends_on(TaskId("generate-headers")));
        CHECK_FALSE(task.depends_on(TaskId("other-task")));
    }

    SECTION("Empty executable path throws std::invalid_argument") {
        REQUIRE_THROWS_AS(
            TaskDefinition::create(TaskId("test"), "desc", ""),
            std::invalid_argument);
    }

    SECTION("Self-dependency throws std::invalid_argument") {
        REQUIRE_THROWS_AS(
            TaskDefinition::create(
                TaskId("self-loop"),
                "desc",
                "binary.exe",
                {},
                std::nullopt,
                {TaskId("self-loop")}),
            std::invalid_argument);
    }

    SECTION("Duplicate prerequisites throw std::invalid_argument") {
        REQUIRE_THROWS_AS(
            TaskDefinition::create(
                TaskId("task"),
                "desc",
                "binary.exe",
                {},
                std::nullopt,
                {TaskId("dep1"), TaskId("dep2"), TaskId("dep1")}),
            std::invalid_argument);
    }

    SECTION("Value semantics: copy, move, and assignment (ADR 0001)") {
        auto original = TaskDefinition::create(
            TaskId("original"),
            "original task",
            "clang.exe",
            {"-O3"});

        // Copy construction
        TaskDefinition copied = original;
        CHECK(copied.id() == original.id());
        CHECK(copied.executable() == original.executable());

        // Copy assignment
        TaskDefinition assigned = TaskDefinition::create(TaskId("temp"), "temp", "cmd.exe");
        assigned = original;
        CHECK(assigned.id() == original.id());

        // Move construction
        TaskDefinition moved = std::move(copied);
        CHECK(moved.id() == TaskId("original"));

        // Move assignment
        assigned = std::move(moved);
        CHECK(assigned.id() == TaskId("original"));

        // Moved-from object can be safely reassigned
        moved = TaskDefinition::create(TaskId("new-task"), "new desc", "tool.exe");
        CHECK(moved.id() == TaskId("new-task"));
    }
}
