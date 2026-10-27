#pragma once

#include "taskforge/core/task_id.hpp"
#include "taskforge/core/task_state.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace taskforge::core {

/**
 * @brief Authoritative immutable post-execution record for a single task.
 *
 * Guaranteed Invariants:
 * - final_state is strictly terminal (Succeeded, Failed, or Blocked).
 * - Succeeded: failure_category is None, exit_code is present (0).
 * - Blocked: failure_category is None, exit_code is nullopt, duration is 0.
 * - Failed (LaunchError): exit_code is nullopt, os_error_code is present.
 * - Failed (NonZeroExit): exit_code is present (!= 0), os_error_code is nullopt.
 * - Failed (SystemFault): exit_code is nullopt, error_message contains fault detail.
 */
class TaskResult {
public:
    /// @brief Creates a result for a successfully executed task.
    [[nodiscard]] static TaskResult create_succeeded(
        TaskId id,
        std::optional<int> exit_code,
        std::string stdout_content,
        std::string stderr_content,
        std::chrono::system_clock::time_point start_time,
        std::chrono::system_clock::time_point end_time,
        std::chrono::milliseconds duration);

    /// @brief Creates a result for a task that failed execution or launch.
    [[nodiscard]] static TaskResult create_failed(
        TaskId id,
        FailureCategory category,
        std::optional<int> exit_code,
        std::optional<unsigned long> os_error_code,
        std::string stdout_content,
        std::string stderr_content,
        std::string error_message,
        std::chrono::system_clock::time_point start_time,
        std::chrono::system_clock::time_point end_time,
        std::chrono::milliseconds duration);

    /// @brief Creates a result for a task that was blocked and never dispatched.
    [[nodiscard]] static TaskResult create_blocked(
        TaskId id,
        std::string reason);

    // Regular value semantics
    TaskResult(const TaskResult&) = default;
    TaskResult& operator=(const TaskResult&) = default;
    TaskResult(TaskResult&&) noexcept = default;
    TaskResult& operator=(TaskResult&&) noexcept = default;
    ~TaskResult() = default;

    // Queries
    [[nodiscard]] const TaskId& task_id() const noexcept { return task_id_; }
    [[nodiscard]] TaskState final_state() const noexcept { return final_state_; }
    [[nodiscard]] FailureCategory failure_category() const noexcept { return failure_category_; }
    [[nodiscard]] std::optional<int> exit_code() const noexcept { return exit_code_; }
    [[nodiscard]] std::optional<unsigned long> os_error_code() const noexcept { return os_error_code_; }
    [[nodiscard]] const std::string& standard_output() const noexcept { return standard_output_; }
    [[nodiscard]] const std::string& standard_error() const noexcept { return standard_error_; }
    [[nodiscard]] const std::optional<std::string>& error_message() const noexcept { return error_message_; }
    [[nodiscard]] std::chrono::system_clock::time_point start_time() const noexcept { return start_time_; }
    [[nodiscard]] std::chrono::system_clock::time_point end_time() const noexcept { return end_time_; }
    [[nodiscard]] std::chrono::milliseconds duration() const noexcept { return duration_; }

    [[nodiscard]] bool is_success() const noexcept { return final_state_ == TaskState::Succeeded; }
    [[nodiscard]] bool is_failed() const noexcept { return final_state_ == TaskState::Failed; }
    [[nodiscard]] bool is_blocked() const noexcept { return final_state_ == TaskState::Blocked; }
    [[nodiscard]] bool is_launch_failure() const noexcept {
        return failure_category_ == FailureCategory::LaunchError;
    }
    [[nodiscard]] bool is_execution_failure() const noexcept {
        return failure_category_ == FailureCategory::NonZeroExit;
    }

private:
    TaskResult(
        TaskId id,
        TaskState state,
        FailureCategory category,
        std::optional<int> exit_code,
        std::optional<unsigned long> os_error_code,
        std::string stdout_content,
        std::string stderr_content,
        std::optional<std::string> error_message,
        std::chrono::system_clock::time_point start_time,
        std::chrono::system_clock::time_point end_time,
        std::chrono::milliseconds duration);

    TaskId task_id_;
    TaskState final_state_{TaskState::Pending};
    FailureCategory failure_category_{FailureCategory::None};
    std::optional<int> exit_code_{std::nullopt};
    std::optional<unsigned long> os_error_code_{std::nullopt};
    std::string standard_output_;
    std::string standard_error_;
    std::optional<std::string> error_message_{std::nullopt};
    std::chrono::system_clock::time_point start_time_{};
    std::chrono::system_clock::time_point end_time_{};
    std::chrono::milliseconds duration_{0};
};

} // namespace taskforge::core
