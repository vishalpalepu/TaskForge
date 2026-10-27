#include "taskforge/core/dependency_graph.hpp"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using namespace taskforge::core;

TEST_CASE("DependencyGraph analysis and cycle detection", "[domain][graph]") {
    SECTION("Linear chain graph: A -> B -> C") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task-a"), "Task A", "tool.exe"));
        tasks.push_back(TaskDefinition::create(TaskId("task-b"), "Task B", "tool.exe", {}, std::nullopt, {TaskId("task-a")}));
        tasks.push_back(TaskDefinition::create(TaskId("task-c"), "Task C", "tool.exe", {}, std::nullopt, {TaskId("task-b")}));

        auto workflow = WorkflowDefinition::create("linear", std::move(tasks));
        DependencyGraph graph(workflow);

        CHECK_FALSE(graph.has_cycles());
        CHECK(graph.detected_cycles().empty());

        // In-degrees
        CHECK(graph.in_degree(TaskId("task-a")) == 0);
        CHECK(graph.in_degree(TaskId("task-b")) == 1);
        CHECK(graph.in_degree(TaskId("task-c")) == 1);

        // Root tasks
        auto roots = graph.root_tasks();
        REQUIRE(roots.size() == 1);
        CHECK(roots[0] == TaskId("task-a"));

        // Forward dependents
        auto deps_a = graph.dependents_of(TaskId("task-a"));
        REQUIRE(deps_a.size() == 1);
        CHECK(deps_a[0] == TaskId("task-b"));

        // Prerequisites
        auto prereqs_c = graph.prerequisites_of(TaskId("task-c"));
        REQUIRE(prereqs_c.size() == 1);
        CHECK(prereqs_c[0] == TaskId("task-b"));
    }

    SECTION("Diamond graph: A -> B, C; B, C -> D") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task-a"), "Root A", "tool.exe"));
        tasks.push_back(TaskDefinition::create(TaskId("task-b"), "Branch B", "tool.exe", {}, std::nullopt, {TaskId("task-a")}));
        tasks.push_back(TaskDefinition::create(TaskId("task-c"), "Branch C", "tool.exe", {}, std::nullopt, {TaskId("task-a")}));
        tasks.push_back(TaskDefinition::create(TaskId("task-d"), "Join D", "tool.exe", {}, std::nullopt, {TaskId("task-b"), TaskId("task-c")}));

        auto workflow = WorkflowDefinition::create("diamond", std::move(tasks));
        DependencyGraph graph(workflow);

        CHECK_FALSE(graph.has_cycles());
        CHECK(graph.in_degree(TaskId("task-a")) == 0);
        CHECK(graph.in_degree(TaskId("task-b")) == 1);
        CHECK(graph.in_degree(TaskId("task-c")) == 1);
        CHECK(graph.in_degree(TaskId("task-d")) == 2);

        auto roots = graph.root_tasks();
        REQUIRE(roots.size() == 1);
        CHECK(roots[0] == TaskId("task-a"));

        auto deps_a = graph.dependents_of(TaskId("task-a"));
        CHECK(deps_a.size() == 2);
    }

    SECTION("Direct 2-node cycle: A -> B -> A") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("node-a"), "A", "tool.exe", {}, std::nullopt, {TaskId("node-b")}));
        tasks.push_back(TaskDefinition::create(TaskId("node-b"), "B", "tool.exe", {}, std::nullopt, {TaskId("node-a")}));

        auto workflow = WorkflowDefinition::create("2-cycle", std::move(tasks));
        DependencyGraph graph(workflow);

        REQUIRE(graph.has_cycles());
        REQUIRE(graph.detected_cycles().size() == 1);

        const auto& cycle = graph.detected_cycles().front();
        // Cycle must begin and end at the same task
        REQUIRE(cycle.size() == 3);
        CHECK(cycle.front() == cycle.back());

        // Exact deterministic sequence matching declaration traversal
        const std::vector<TaskId> expected_cycle = {
            TaskId("node-a"), TaskId("node-b"), TaskId("node-a")
        };
        CHECK(cycle == expected_cycle);

        // Verify each consecutive pair corresponds to a genuine dependency edge
        for (size_t i = 0; i + 1 < cycle.size(); ++i) {
            const auto& current = cycle[i];
            const auto& next = cycle[i + 1];
            const auto& prereqs = graph.prerequisites_of(current);
            CHECK(std::ranges::find(prereqs, next) != prereqs.end());
        }
    }

    SECTION("Indirect 3-node cycle: A -> C -> B -> A") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("node-a"), "A", "tool.exe", {}, std::nullopt, {TaskId("node-c")}));
        tasks.push_back(TaskDefinition::create(TaskId("node-b"), "B", "tool.exe", {}, std::nullopt, {TaskId("node-a")}));
        tasks.push_back(TaskDefinition::create(TaskId("node-c"), "C", "tool.exe", {}, std::nullopt, {TaskId("node-b")}));

        auto workflow = WorkflowDefinition::create("3-cycle", std::move(tasks));
        DependencyGraph graph(workflow);

        REQUIRE(graph.has_cycles());
        REQUIRE_FALSE(graph.detected_cycles().empty());

        const auto& cycle = graph.detected_cycles().front();
        REQUIRE(cycle.size() == 4);
        CHECK(cycle.front() == cycle.back());

        // Exact deterministic sequence: A requires C, C requires B, B requires A
        const std::vector<TaskId> expected_cycle = {
            TaskId("node-a"), TaskId("node-c"), TaskId("node-b"), TaskId("node-a")
        };
        CHECK(cycle == expected_cycle);

        // Verify each consecutive pair is a genuine dependency edge
        for (size_t i = 0; i + 1 < cycle.size(); ++i) {
            const auto& current = cycle[i];
            const auto& next = cycle[i + 1];
            const auto& prereqs = graph.prerequisites_of(current);
            CHECK(std::ranges::find(prereqs, next) != prereqs.end());
        }
    }

    SECTION("Multi-component graph with an isolated cycle") {
        // Component 1 (valid): A -> B
        // Component 2 (cycle): C -> D -> C
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("clean-a"), "clean a", "tool.exe"));
        tasks.push_back(TaskDefinition::create(TaskId("clean-b"), "clean b", "tool.exe", {}, std::nullopt, {TaskId("clean-a")}));
        tasks.push_back(TaskDefinition::create(TaskId("cycle-c"), "cycle c", "tool.exe", {}, std::nullopt, {TaskId("cycle-d")}));
        tasks.push_back(TaskDefinition::create(TaskId("cycle-d"), "cycle d", "tool.exe", {}, std::nullopt, {TaskId("cycle-c")}));

        auto workflow = WorkflowDefinition::create("multi-component", std::move(tasks));
        DependencyGraph graph(workflow);

        REQUIRE(graph.has_cycles());
        REQUIRE(graph.detected_cycles().size() == 1);

        const auto& cycle = graph.detected_cycles().front();
        const std::vector<TaskId> expected_cycle = {
            TaskId("cycle-c"), TaskId("cycle-d"), TaskId("cycle-c")
        };
        CHECK(cycle == expected_cycle);
    }
}
