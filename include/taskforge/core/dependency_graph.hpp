#pragma once

#include "taskforge/core/task_id.hpp"
#include "taskforge/core/workflow_definition.hpp"

#include <unordered_map>
#include <vector>

namespace taskforge::core {

/**
 * @brief Represents the directed dependency graph of a workflow.
 *
 * Implements deterministic cycle detection using 3-color Depth First Search (DFS).
 *
 * Graph Edge Conventions:
 * - Prerequisite edge: B depends on A (A must precede B).
 * - Forward edge (Adjacency): A -> B (A's completion unlocks B).
 * - Backward edge (Prerequisites): B -> A (B requires A to complete).
 */
class DependencyGraph {
public:
    /// @brief Constructs and analyzes the dependency graph for the given workflow.
    explicit DependencyGraph(const WorkflowDefinition& workflow);

    /// @brief Returns true if any directed cycles were detected.
    [[nodiscard]] bool has_cycles() const noexcept { return !detected_cycles_.empty(); }

    /// @brief Returns all cycles detected during analysis.
    /// Each cycle is represented as a closed sequence of TaskIds, e.g. [A, B, A].
    [[nodiscard]] const std::vector<std::vector<TaskId>>& detected_cycles() const noexcept {
        return detected_cycles_;
    }

    /// @brief Returns the list of tasks that directly depend on the given task (forward edges).
    [[nodiscard]] const std::vector<TaskId>& dependents_of(const TaskId& id) const;

    /// @brief Returns the prerequisites that must finish before the given task (backward edges).
    [[nodiscard]] const std::vector<TaskId>& prerequisites_of(const TaskId& id) const;

    /// @brief Returns the in-degree (number of unmet prerequisites) of a task.
    [[nodiscard]] size_t in_degree(const TaskId& id) const;

    /// @brief Returns all tasks that have zero prerequisites (eligible to run first).
    [[nodiscard]] std::vector<TaskId> root_tasks() const;

private:
    enum class NodeColor {
        White, // Unvisited
        Gray,  // Visiting (currently on recursion stack)
        Black  // Visited (completely explored)
    };

    void build_graph(const WorkflowDefinition& workflow);
    void detect_cycles(const WorkflowDefinition& workflow);
    void dfs_visit(
        const TaskId& node,
        std::unordered_map<TaskId, NodeColor>& colors,
        std::vector<TaskId>& call_stack);

    // Forward adjacency: Task -> dependents that require it
    std::unordered_map<TaskId, std::vector<TaskId>> forward_edges_;

    // Backward adjacency: Task -> prerequisites it requires
    std::unordered_map<TaskId, std::vector<TaskId>> backward_edges_;

    // In-degree: Task -> number of prerequisites
    std::unordered_map<TaskId, size_t> in_degrees_;

    // List of detected cycles
    std::vector<std::vector<TaskId>> detected_cycles_;

    // Empty static vector to return for nonexistent queries
    static const std::vector<TaskId> empty_task_list_;
};

} // namespace taskforge::core
