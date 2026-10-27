#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace taskforge::scheduler {

/**
 * @brief Parameters for executing a child process.
 */
struct ExecutionRequest {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::optional<std::filesystem::path> working_directory{std::nullopt};
    std::optional<std::chrono::milliseconds> timeout{std::nullopt};
};

/**
 * @brief Authoritative outcome of an execution attempt returned by IProcessExecutor.
 *
 * Enforces mutual exclusivity through static factory methods:
 * - completed: exit code is known, output is captured.
 * - launch_failure: OS failed to spawn process, OS error code is captured.
 * - system_fault: unexpected execution exception or fatal IO failure.
 */
class ExecutionResponse {
public:
    [[nodiscard]] static ExecutionResponse completed(
        int exit_code,
        std::string stdout_content,
        std::string stderr_content,
        std::chrono::milliseconds duration);

    [[nodiscard]] static ExecutionResponse launch_failure(
        unsigned long os_error_code,
        std::string error_message,
        std::chrono::milliseconds duration = std::chrono::milliseconds{0});

    [[nodiscard]] static ExecutionResponse system_fault(
        std::string error_message,
        std::chrono::milliseconds duration = std::chrono::milliseconds{0});

    // Regular value semantics
    ExecutionResponse(const ExecutionResponse&) = default;
    ExecutionResponse& operator=(const ExecutionResponse&) = default;
    ExecutionResponse(ExecutionResponse&&) noexcept = default;
    ExecutionResponse& operator=(ExecutionResponse&&) noexcept = default;
    ~ExecutionResponse() = default;

    // Queries
    [[nodiscard]] bool is_successful() const noexcept {
        return exit_code_.has_value() && *exit_code_ == 0;
    }
    [[nodiscard]] bool is_launch_failure() const noexcept {
        return os_error_code_.has_value();
    }
    [[nodiscard]] bool is_system_fault() const noexcept {
        return !exit_code_.has_value() && !os_error_code_.has_value();
    }

    [[nodiscard]] std::optional<int> exit_code() const noexcept { return exit_code_; }
    [[nodiscard]] std::optional<unsigned long> os_error_code() const noexcept { return os_error_code_; }
    [[nodiscard]] const std::string& standard_output() const noexcept { return standard_output_; }
    [[nodiscard]] const std::string& standard_error() const noexcept { return standard_error_; }
    [[nodiscard]] const std::string& error_message() const noexcept { return error_message_; }
    [[nodiscard]] std::chrono::milliseconds duration() const noexcept { return duration_; }

private:
    ExecutionResponse(
        std::optional<int> exit_code,
        std::optional<unsigned long> os_error_code,
        std::string stdout_content,
        std::string stderr_content,
        std::string error_message,
        std::chrono::milliseconds duration);

    std::optional<int> exit_code_{std::nullopt};
    std::optional<unsigned long> os_error_code_{std::nullopt};
    std::string standard_output_;
    std::string standard_error_;
    std::string error_message_;
    std::chrono::milliseconds duration_{0};
};

/**
 * @brief Abstract boundary isolating process invocation from workflow scheduling.
 *
 * Implements Dependency Inversion Principle (DIP):
 * High-level scheduler depends on this abstraction, not concrete OS process APIs.
 */
class IProcessExecutor {
public:
    IProcessExecutor() = default;
    virtual ~IProcessExecutor() = default;

    IProcessExecutor(const IProcessExecutor&) = delete;
    IProcessExecutor& operator=(const IProcessExecutor&) = delete;
    IProcessExecutor(IProcessExecutor&&) = delete;
    IProcessExecutor& operator=(IProcessExecutor&&) = delete;

    /**
     * @brief Executes a single process according to the request parameters.
     * @param request Specification of the executable, arguments, and environment.
     * @return Authoritative ExecutionResponse recording the outcome.
     */
    [[nodiscard]] virtual ExecutionResponse execute(const ExecutionRequest& request) = 0;
};

} // namespace taskforge::scheduler
