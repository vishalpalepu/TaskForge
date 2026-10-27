#pragma once

#include <iosfwd>
#include <string_view>

namespace taskforge::core {

/**
 * @brief Discrete lifecycle states for a task during workflow execution.
 */
enum class TaskState {
    Pending,   ///< Waiting for prerequisite tasks to evaluate.
    Ready,     ///< All prerequisites have succeeded; eligible to execute in ready_queue.
    Running,   ///< Process has been dispatched to executor and is executing.
    Succeeded, ///< Terminal: Process exited cleanly with exit code 0.
    Failed,    ///< Terminal: Process returned non-zero code, failed to launch, or crashed.
    Blocked    ///< Terminal: Task was aborted and never launched because a prerequisite failed/blocked.
};

/**
 * @brief Specific category of failure preserving failure diagnostics (ADR 0003).
 */
enum class FailureCategory {
    None,        ///< Task succeeded, is blocked, or is not in a failed state.
    LaunchError, ///< OS failed to spawn process (e.g. executable not found, invalid path).
    NonZeroExit, ///< Process executed and terminated with non-zero exit code.
    SystemFault, ///< Unexpected executor exception or system pipe/IO error.
    Timeout      ///< Process exceeded maximum configured wall-clock time (future milestone).
};

/// @brief Returns a string representation of a TaskState.
[[nodiscard]] std::string_view to_string(TaskState state) noexcept;

/// @brief Returns a string representation of a FailureCategory.
[[nodiscard]] std::string_view to_string(FailureCategory category) noexcept;

/// @brief Stream output operator for TaskState.
std::ostream& operator<<(std::ostream& os, TaskState state);

/// @brief Stream output operator for FailureCategory.
std::ostream& operator<<(std::ostream& os, FailureCategory category);

} // namespace taskforge::core
