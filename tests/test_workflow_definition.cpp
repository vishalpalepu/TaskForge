#include "taskforge/core/workflow_definition.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace taskforge::core;

TEST_CASE("WorkflowDefinition aggregate and indexing", "[domain][workflow_definition]") {
    SECTION("Valid workflow establishes declaration indices sequentially (ADR 0002)") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("configure"), "configure cmake", "cmake"));
        tasks.push_back(TaskDefinition::create(TaskId("build"), "compile binaries", "cmake", {"--build", "."}));
        tasks.push_back(TaskDefinition::create(TaskId("test"), "run ctest", "ctest"));

        auto workflow = WorkflowDefinition::create("build-pipeline", std::move(tasks));

        CHECK(workflow.name() == "build-pipeline");
        CHECK(workflow.task_count() == 3);
        CHECK_FALSE(workflow.empty());

        // Check assigned declaration indices
        CHECK(workflow.declaration_index_of(TaskId("configure")) == 0);
        CHECK(workflow.declaration_index_of(TaskId("build")) == 1);
        CHECK(workflow.declaration_index_of(TaskId("test")) == 2);

        // Check task definitions received matching indices
        CHECK(workflow.tasks()[0].declaration_index() == 0);
        CHECK(workflow.tasks()[1].declaration_index() == 1);
        CHECK(workflow.tasks()[2].declaration_index() == 2);
    }

    SECTION("Duplicate task IDs throw std::invalid_argument") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("same-id"), "first", "tool1.exe"));
        tasks.push_back(TaskDefinition::create(TaskId("same-id"), "second", "tool2.exe"));

        REQUIRE_THROWS_AS(
            WorkflowDefinition::create("duplicate-workflow", std::move(tasks)),
            std::invalid_argument);
    }

    SECTION("Task lookup operations") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task-alpha"), "alpha", "alpha.exe"));
        tasks.push_back(TaskDefinition::create(TaskId("task-beta"), "beta", "beta.exe"));

        auto workflow = WorkflowDefinition::create("lookup-test", std::move(tasks));

        CHECK(workflow.has_task(TaskId("task-alpha")));
        CHECK(workflow.has_task(TaskId("task-beta")));
        CHECK_FALSE(workflow.has_task(TaskId("nonexistent")));

        const auto* found = workflow.find_task(TaskId("task-alpha"));
        REQUIRE(found != nullptr);
        CHECK(found->description() == "alpha");

        CHECK(workflow.find_task(TaskId("missing")) == nullptr);
        REQUIRE_THROWS_AS(workflow.declaration_index_of(TaskId("missing")), std::out_of_range);
    }

    SECTION("Empty workflow aggregate") {
        auto workflow = WorkflowDefinition::create("empty", {});
        CHECK(workflow.empty());
        CHECK(workflow.task_count() == 0);
    }

    SECTION("Value semantics (ADR 0001)") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task-1"), "desc", "tool.exe"));

        auto original = WorkflowDefinition::create("original", std::move(tasks));
        WorkflowDefinition copied = original;
        CHECK(copied.name() == "original");
        CHECK(copied.task_count() == 1);

        WorkflowDefinition moved = std::move(copied);
        CHECK(moved.name() == "original");
        CHECK(moved.task_count() == 1);
    }
}
