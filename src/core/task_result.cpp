#include "taskforge/core/task_result.hpp"

namespace taskforge::core {

TaskResult TaskResult::create_succeeded(
    TaskId id,
    std::optional<int> exit_code,
    std::string stdout_content,
    std::string stderr_content,
    std::chrono::system_clock::time_point start_time,
    std::chrono::system_clock::time_point end_time,
    std::chrono::milliseconds duration) {

    return TaskResult{
        std::move(id),
        TaskState::Succeeded,
        FailureCategory::None,
        exit_code.has_value() ? exit_code : std::optional<int>(0),
        std::nullopt,
        std::move(stdout_content),
        std::move(stderr_content),
        std::nullopt,
        start_time,
        end_time,
        duration};
}

TaskResult TaskResult::create_failed(
    TaskId id,
    FailureCategory category,
    std::optional<int> exit_code,
    std::optional<unsigned long> os_error_code,
    std::string stdout_content,
    std::string stderr_content,
    std::string error_message,
    std::chrono::system_clock::time_point start_time,
    std::chrono::system_clock::time_point end_time,
    std::chrono::milliseconds duration) {

    return TaskResult{
        std::move(id),
        TaskState::Failed,
        category,
        exit_code,
        os_error_code,
        std::move(stdout_content),
        std::move(stderr_content),
        std::make_optional(std::move(error_message)),
        start_time,
        end_time,
        duration};
}

TaskResult TaskResult::create_blocked(
    TaskId id,
    std::string reason) {

    auto now = std::chrono::system_clock::now();
    return TaskResult{
        std::move(id),
        TaskState::Blocked,
        FailureCategory::None,
        std::nullopt,
        std::nullopt,
        "",
        "",
        std::make_optional(std::move(reason)),
        now,
        now,
        std::chrono::milliseconds{0}};
}

TaskResult::TaskResult(
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
    std::chrono::milliseconds duration)
    : task_id_(std::move(id)),
      final_state_(state),
      failure_category_(category),
      exit_code_(exit_code),
      os_error_code_(os_error_code),
      standard_output_(std::move(stdout_content)),
      standard_error_(std::move(stderr_content)),
      error_message_(std::move(error_message)),
      start_time_(start_time),
      end_time_(end_time),
      duration_(duration) {}

} // namespace taskforge::core
