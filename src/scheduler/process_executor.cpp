#include "taskforge/scheduler/process_executor.hpp"

#include <stdexcept>

namespace taskforge::scheduler {

ExecutionResponse ExecutionResponse::completed(
    int exit_code,
    std::string stdout_content,
    std::string stderr_content,
    std::chrono::milliseconds duration) {

    if (duration < std::chrono::milliseconds{0}) {
        throw std::invalid_argument("Execution duration cannot be negative");
    }

    return ExecutionResponse{
        std::make_optional(exit_code),
        std::nullopt,
        std::move(stdout_content),
        std::move(stderr_content),
        "",
        duration};
}

ExecutionResponse ExecutionResponse::launch_failure(
    unsigned long os_error_code,
    std::string error_message,
    std::chrono::milliseconds duration) {

    if (duration < std::chrono::milliseconds{0}) {
        throw std::invalid_argument("Execution duration cannot be negative");
    }

    return ExecutionResponse{
        std::nullopt,
        std::make_optional(os_error_code),
        "",
        "",
        std::move(error_message),
        duration};
}

ExecutionResponse ExecutionResponse::system_fault(
    std::string error_message,
    std::chrono::milliseconds duration) {

    if (duration < std::chrono::milliseconds{0}) {
        throw std::invalid_argument("Execution duration cannot be negative");
    }

    return ExecutionResponse{
        std::nullopt,
        std::nullopt,
        "",
        "",
        std::move(error_message),
        duration};
}

ExecutionResponse::ExecutionResponse(
    std::optional<int> exit_code,
    std::optional<unsigned long> os_error_code,
    std::string stdout_content,
    std::string stderr_content,
    std::string error_message,
    std::chrono::milliseconds duration)
    : exit_code_(exit_code),
      os_error_code_(os_error_code),
      standard_output_(std::move(stdout_content)),
      standard_error_(std::move(stderr_content)),
      error_message_(std::move(error_message)),
      duration_(duration) {}

} // namespace taskforge::scheduler
