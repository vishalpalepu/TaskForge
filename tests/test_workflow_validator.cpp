#include "taskforge/core/workflow_validator.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace taskforge::core;

TEST_CASE("WorkflowValidator rule enforcement", "[domain][validator]") {
    SECTION("Valid acyclic workflow passes validation") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("build"), "compile", "cmake", {"--build", "."}));
        tasks.push_back(TaskDefinition::create(TaskId("test"), "run tests", "ctest", {}, std::nullopt, {TaskId("build")}));

        auto workflow = WorkflowDefinition::create("valid-pipeline", std::move(tasks));
        auto result = WorkflowValidator::validate(workflow);

        CHECK(result.is_valid());
        CHECK(static_cast<bool>(result));
        CHECK(result.errors().empty());
    }

    SECTION("Empty workflow fails with EmptyWorkflow error") {
        auto workflow = WorkflowDefinition::create("empty-pipeline", {});
        auto result = WorkflowValidator::validate(workflow);

        CHECK_FALSE(result.is_valid());
        CHECK(result.has_error_code(ValidationErrorCode::EmptyWorkflow));
        REQUIRE(result.errors().size() == 1);
        CHECK(result.errors()[0].code == ValidationErrorCode::EmptyWorkflow);
    }

    SECTION("Missing dependency is detected and reported") {
        std::vector<TaskDefinition> tasks;
        // Task depends on "nonexistent-task"
        tasks.push_back(TaskDefinition::create(
            TaskId("test"),
            "run tests",
            "ctest",
            {},
            std::nullopt,
            {TaskId("nonexistent-task")}));

        auto workflow = WorkflowDefinition::create("broken-deps", std::move(tasks));
        auto result = WorkflowValidator::validate(workflow);

        CHECK_FALSE(result.is_valid());
        CHECK(result.has_error_code(ValidationErrorCode::MissingDependency));
        REQUIRE(result.errors().size() == 1);
        CHECK(result.errors()[0].task_id == TaskId("test"));
    }

    SECTION("Duplicate task IDs detected via validate_raw_tasks") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("duplicate"), "first", "tool1.exe"));
        tasks.push_back(TaskDefinition::create(TaskId("duplicate"), "second", "tool2.exe"));

        auto result = WorkflowValidator::validate_raw_tasks("dup-check", tasks);

        CHECK_FALSE(result.is_valid());
        CHECK(result.has_error_code(ValidationErrorCode::DuplicateTaskId));
    }

    SECTION("Circular dependency is detected and reported with cycle path") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task-a"), "A", "tool.exe", {}, std::nullopt, {TaskId("task-b")}));
        tasks.push_back(TaskDefinition::create(TaskId("task-b"), "B", "tool.exe", {}, std::nullopt, {TaskId("task-a")}));

        auto workflow = WorkflowDefinition::create("cycle-check", std::move(tasks));
        auto result = WorkflowValidator::validate(workflow);

        CHECK_FALSE(result.is_valid());
        CHECK(result.has_error_code(ValidationErrorCode::CircularDependency));
        REQUIRE_FALSE(result.errors().empty());

        // Check cycle path is populated
        bool found_cycle_path = false;
        for (const auto& err : result.errors()) {
            if (err.code == ValidationErrorCode::CircularDependency && !err.cycle_path.empty()) {
                found_cycle_path = true;
                CHECK(err.cycle_path.front() == err.cycle_path.back());
            }
        }
        CHECK(found_cycle_path);
    }

    SECTION("Deterministic manifest declaration ordering (ADR 0002)") {
        // Deliberately declared in arbitrary order: C, A, B
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task-c"), "Task C", "tool.exe"));
        tasks.push_back(TaskDefinition::create(TaskId("task-a"), "Task A", "tool.exe"));
        tasks.push_back(TaskDefinition::create(TaskId("task-b"), "Task B", "tool.exe"));

        auto workflow = WorkflowDefinition::create("ordering-test", std::move(tasks));

        // Declaration index must match declared order, NOT alphabetical order
        CHECK(workflow.declaration_index_of(TaskId("task-c")) == 0);
        CHECK(workflow.declaration_index_of(TaskId("task-a")) == 1);
        CHECK(workflow.declaration_index_of(TaskId("task-b")) == 2);

        CHECK(workflow.tasks()[0].id() == TaskId("task-c"));
        CHECK(workflow.tasks()[1].id() == TaskId("task-a"));
        CHECK(workflow.tasks()[2].id() == TaskId("task-b"));
    }

    SECTION("Multiple errors are accumulated together") {
        std::vector<TaskDefinition> tasks;
        // Error 1: Missing dependency
        tasks.push_back(TaskDefinition::create(TaskId("task-1"), "T1", "tool.exe", {}, std::nullopt, {TaskId("missing-dep")}));
        // Error 2: Cycle with task-3
        tasks.push_back(TaskDefinition::create(TaskId("task-2"), "T2", "tool.exe", {}, std::nullopt, {TaskId("task-3")}));
        tasks.push_back(TaskDefinition::create(TaskId("task-3"), "T3", "tool.exe", {}, std::nullopt, {TaskId("task-2")}));

        auto workflow = WorkflowDefinition::create("multi-error", std::move(tasks));
        auto result = WorkflowValidator::validate(workflow);

        CHECK_FALSE(result.is_valid());
        CHECK(result.has_error_code(ValidationErrorCode::MissingDependency));
        CHECK(result.has_error_code(ValidationErrorCode::CircularDependency));
        CHECK(result.errors().size() >= 2);
    }
}
