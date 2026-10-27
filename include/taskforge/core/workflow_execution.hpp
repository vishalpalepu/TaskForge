#pragma once

#include "taskforge/core/task_id.hpp"
#include "taskforge/core/task_result.hpp"
#include "taskforge/core/task_state.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace taskforge::core {

/**
 * @brief Authoritative runtime aggregate recording the outcome of a completed workflow execution.
 *
 * Implements single source of truth:
 * - Owns a single map of TaskResult objects.
 * - All task state queries are derived directly from the corresponding TaskResult.
 * - Every task in the workflow has an entry in this aggregate upon completion.
 */
class WorkflowExecution {
public:
    WorkflowExecution(
        std::string workflow_name,
        std::unordered_map<TaskId, TaskResult> results,
        std::vector<TaskId> execution_order,
        std::chrono::system_clock::time_point start_time,
        std::chrono::system_clock::time_point end_time);

    // Regular value semantics
    WorkflowExecution(const WorkflowExecution&) = default;
    WorkflowExecution& operator=(const WorkflowExecution&) = default;
    WorkflowExecution(WorkflowExecution&&) noexcept = default;
    WorkflowExecution& operator=(WorkflowExecution&&) noexcept = default;
    ~WorkflowExecution() = default;

    // Observers
    [[nodiscard]] const std::string& workflow_name() const noexcept { return workflow_name_; }
    [[nodiscard]] std::chrono::system_clock::time_point start_time() const noexcept { return start_time_; }
    [[nodiscard]] std::chrono::system_clock::time_point end_time() const noexcept { return end_time_; }
    [[nodiscard]] const std::unordered_map<TaskId, TaskResult>& all_results() const noexcept { return results_; }
    [[nodiscard]] const std::vector<TaskId>& execution_order() const noexcept { return execution_order_; }
    [[nodiscard]] size_t task_count() const noexcept { return results_.size(); }

    /// @brief Queries the final terminal state of a specific task.
    /// @throws std::out_of_range if task_id does not exist in the execution results.
    [[nodiscard]] TaskState task_state(const TaskId& id) const;

    /// @brief Finds the task result for a given task, returning nullptr if not found.
    [[nodiscard]] const TaskResult* task_result(const TaskId& id) const noexcept;

    /// @brief True if every task in the workflow succeeded cleanly.
    [[nodiscard]] bool is_successful() const noexcept;

    /// @brief True if any task failed or was blocked.
    [[nodiscard]] bool has_failures() const noexcept;

    /// @brief Returns the total wall-clock elapsed duration of the workflow execution.
    [[nodiscard]] std::chrono::milliseconds total_duration() const noexcept;

    /// @brief Counts the number of tasks in the given terminal state.
    [[nodiscard]] size_t count_with_state(TaskState state) const noexcept;

private:
    std::string workflow_name_;
    std::unordered_map<TaskId, TaskResult> results_;
    std::vector<TaskId> execution_order_;
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
};

} // namespace taskforge::core
