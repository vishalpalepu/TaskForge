#pragma once

#include "taskforge/core/validation_error.hpp"
#include "taskforge/core/workflow_definition.hpp"
#include "taskforge/core/workflow_execution.hpp"
#include "taskforge/scheduler/process_executor.hpp"

#include <variant>

namespace taskforge::scheduler {

/**
 * @brief Authoritative discriminated outcome of running a workflow through WorkflowScheduler.
 *
 * Guarantees mutual exclusivity:
 * - If workflow definition failed validation: contains core::ValidationResult, no execution aggregate exists.
 * - If workflow was valid: contains core::WorkflowExecution, recording every task's terminal state.
 * - An execution with failed/blocked tasks is valid (is_valid() == true), but not successful (is_successful() == false).
 */
class WorkflowRunResult {
public:
    /// @brief Creates a failed run result from validation diagnostics.
    /// @throws std::invalid_argument if result.is_valid() is true.
    [[nodiscard]] static WorkflowRunResult validation_failure(core::ValidationResult result);
    [[nodiscard]] static WorkflowRunResult executed(core::WorkflowExecution execution);

    // Regular value semantics
    WorkflowRunResult(const WorkflowRunResult&) = default;
    WorkflowRunResult& operator=(const WorkflowRunResult&) = default;
    WorkflowRunResult(WorkflowRunResult&&) noexcept = default;
    WorkflowRunResult& operator=(WorkflowRunResult&&) noexcept = default;
    ~WorkflowRunResult() = default;

    /// @brief True if workflow passed semantic validation and was executed.
    [[nodiscard]] bool is_valid() const noexcept;

    /// @brief True if workflow passed validation and all tasks completed with Succeeded.
    [[nodiscard]] bool is_successful() const noexcept;

    /// @brief Accesses validation diagnostics when is_valid() is false.
    /// @throws std::logic_error if is_valid() is true.
    [[nodiscard]] const core::ValidationResult& validation_result() const;

    /// @brief Accesses execution aggregate when is_valid() is true.
    /// @throws std::logic_error if is_valid() is false.
    [[nodiscard]] const core::WorkflowExecution& execution() const;

    /// @brief Pointer accessor for execution aggregate, or nullptr if validation failed.
    [[nodiscard]] const core::WorkflowExecution* execution_ptr() const noexcept;

private:
    explicit WorkflowRunResult(core::ValidationResult result);
    explicit WorkflowRunResult(core::WorkflowExecution execution);

    std::variant<core::ValidationResult, core::WorkflowExecution> result_;
};

/**
 * @brief Coordinates the sequential execution of tasks within a validated workflow.
 *
 * Implements Single Responsibility Principle (SRP):
 * - Validates workflow semantics before dispatching any processes.
 * - Resolves ready tasks using Kahn's algorithm with deterministic declaration ordering.
 * - Dispatches eligible tasks sequentially to the injected IProcessExecutor.
 * - Propagates Blocked state across transitive dependents upon task failure using pruned BFS.
 * - Strictly non-copyable and non-movable to guard the external executor dependency.
 */
class WorkflowScheduler {
public:
    explicit WorkflowScheduler(IProcessExecutor& executor) noexcept;

    // Strictly non-copyable and non-movable
    WorkflowScheduler(const WorkflowScheduler&) = delete;
    WorkflowScheduler& operator=(const WorkflowScheduler&) = delete;
    WorkflowScheduler(WorkflowScheduler&&) = delete;
    WorkflowScheduler& operator=(WorkflowScheduler&&) = delete;
    ~WorkflowScheduler() = default;

    /**
     * @brief Executes the specified workflow to completion.
     * @param workflow The workflow definition to execute.
     * @return Discriminated WorkflowRunResult.
     */
    [[nodiscard]] WorkflowRunResult run(const core::WorkflowDefinition& workflow);

private:
    IProcessExecutor& executor_;
};

} // namespace taskforge::scheduler
