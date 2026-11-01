#pragma once

#include "taskforge/scheduler/process_executor.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace taskforge::platform::win32 {

/**
 * @brief Concrete implementation of IProcessExecutor for the Windows platform.
 *
 * Coordinates Windows child process creation via CreateProcessW:
 * - Validates UTF-8 input and converts to UTF-16 wide strings.
 * - Resolves executables deterministically: explicit paths against process CWD,
 *   bare names by enumerating sanitized environment PATH directories.
 * - Rejects batch scripts (.cmd, .bat) with an explicit configuration error.
 * - Quotes command-line arguments targeting the Microsoft UCRT parsing standard.
 * - Uses STARTUPINFOEXW and PROC_THREAD_ATTRIBUTE_HANDLE_LIST to whitelist exactly
 *   three standard handles (NUL stdin, stdout write, stderr write).
 * - Spawns concurrent reader threads prior to process launch and coordinates fatal
 *   reader errors using a manual-reset event, terminating the child if needed.
 * - Bounds stream capture to 10 MiB, discarding excess bytes while draining.
 * - Measures execution duration with std::chrono::steady_clock.
 */
class WindowsProcessExecutor final : public scheduler::IProcessExecutor {
public:
    static constexpr size_t kMaxStreamCaptureBytes = 10 * 1024 * 1024; // 10 MiB
    static constexpr size_t kMaxCommandLineLength = 32767;

    WindowsProcessExecutor() = default;
    ~WindowsProcessExecutor() override = default;

    // Non-copyable and non-movable
    WindowsProcessExecutor(const WindowsProcessExecutor&) = delete;
    WindowsProcessExecutor& operator=(const WindowsProcessExecutor&) = delete;
    WindowsProcessExecutor(WindowsProcessExecutor&&) = delete;
    WindowsProcessExecutor& operator=(WindowsProcessExecutor&&) = delete;

    [[nodiscard]] scheduler::ExecutionResponse execute(
        const scheduler::ExecutionRequest& request) override;

    // Static utilities exposed for targeted unit and integration testing
    [[nodiscard]] static std::wstring quote_argument(std::wstring_view arg);

    [[nodiscard]] static std::optional<std::wstring> build_command_line(
        std::wstring_view resolved_executable,
        const std::vector<std::string>& arguments,
        std::string& out_error_message);

    [[nodiscard]] static std::optional<std::filesystem::path> resolve_executable(
        const std::filesystem::path& executable,
        unsigned long& out_error_code,
        std::string& out_error_message);
};

} // namespace taskforge::platform::win32
