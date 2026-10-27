#pragma once

#include "taskforge/core/task_definition.hpp"
#include "taskforge/core/task_id.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace taskforge::core {

/**
 * @brief Aggregate root representing a workflow specification.
 *
 * Adheres to ADR 0001:
 * - Owns all TaskDefinition values in a contiguous std::vector.
 * - Enforces uniqueness of TaskIds within the workflow.
 * - Establishes declaration_index for each task sequentially.
 * - Provides O(1) task lookup via internal index map.
 */
class WorkflowDefinition {
public:
    /// @brief Factory function that constructs a WorkflowDefinition from tasks.
    /// @throws std::invalid_argument if duplicate task IDs are present.
    [[nodiscard]] static WorkflowDefinition create(
        std::string name,
        std::vector<TaskDefinition> tasks);

    // Standard value semantics (ADR 0001)
    WorkflowDefinition(const WorkflowDefinition&) = default;
    WorkflowDefinition& operator=(const WorkflowDefinition&) = default;
    WorkflowDefinition(WorkflowDefinition&&) noexcept = default;
    WorkflowDefinition& operator=(WorkflowDefinition&&) noexcept = default;
    ~WorkflowDefinition() = default;

    // Const-only observers
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::vector<TaskDefinition>& tasks() const noexcept { return tasks_; }
    [[nodiscard]] size_t task_count() const noexcept { return tasks_.size(); }
    [[nodiscard]] bool empty() const noexcept { return tasks_.empty(); }

    /// @brief Checks whether a task with the given ID exists in the workflow.
    [[nodiscard]] bool has_task(const TaskId& id) const noexcept;

    /// @brief Finds a task by ID, returning nullptr if not found.
    [[nodiscard]] const TaskDefinition* find_task(const TaskId& id) const noexcept;

    /// @brief Returns the 0-based declaration order index of a task.
    /// @throws std::out_of_range if task is not found in the workflow.
    [[nodiscard]] size_t declaration_index_of(const TaskId& id) const;

private:
    WorkflowDefinition(
        std::string name,
        std::vector<TaskDefinition> tasks,
        std::unordered_map<TaskId, size_t> lookup);

    std::string name_;
    std::vector<TaskDefinition> tasks_;
    std::unordered_map<TaskId, size_t> lookup_;
};

} // namespace taskforge::core
