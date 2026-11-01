#include "taskforge/platform/win32/windows_process_executor.hpp"
#include "taskforge/manifest/manifest_parser.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace taskforge::platform::win32 {

namespace {

// ---------------------------------------------------------------------------
// RAII Handle Wrapper
// ---------------------------------------------------------------------------

class AutoHandle {
public:
    AutoHandle() noexcept = default;
    explicit AutoHandle(HANDLE h) noexcept : handle_(h) {}
    ~AutoHandle() noexcept { reset(); }

    AutoHandle(const AutoHandle&) = delete;
    AutoHandle& operator=(const AutoHandle&) = delete;

    AutoHandle(AutoHandle&& other) noexcept : handle_(other.release()) {}
    AutoHandle& operator=(AutoHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

    [[nodiscard]] bool is_valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE release() noexcept {
        HANDLE h = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return h;
    }

    void reset(HANDLE new_handle = INVALID_HANDLE_VALUE) noexcept {
        if (is_valid()) {
            ::CloseHandle(handle_);
        }
        handle_ = new_handle;
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

// ---------------------------------------------------------------------------
// RAII Process Thread Attribute List Guard
// ---------------------------------------------------------------------------

class ProcThreadAttributeGuard {
public:
    ProcThreadAttributeGuard() = default;
    ~ProcThreadAttributeGuard() { reset(); }

    ProcThreadAttributeGuard(const ProcThreadAttributeGuard&) = delete;
    ProcThreadAttributeGuard& operator=(const ProcThreadAttributeGuard&) = delete;
    ProcThreadAttributeGuard(ProcThreadAttributeGuard&&) = delete;
    ProcThreadAttributeGuard& operator=(ProcThreadAttributeGuard&&) = delete;

    bool initialize(DWORD attribute_count) {
        reset();
        SIZE_T size = 0;
        ::InitializeProcThreadAttributeList(nullptr, attribute_count, 0, &size);
        if (size == 0) {
            return false;
        }
        raw_mem_ = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(std::malloc(size));
        if (!raw_mem_) {
            return false;
        }
        if (!::InitializeProcThreadAttributeList(raw_mem_, attribute_count, 0, &size)) {
            std::free(raw_mem_);
            raw_mem_ = nullptr;
            return false;
        }
        initialized_ = true;
        return true;
    }

    void reset() noexcept {
        if (raw_mem_) {
            if (initialized_) {
                ::DeleteProcThreadAttributeList(raw_mem_);
                initialized_ = false;
            }
            std::free(raw_mem_);
            raw_mem_ = nullptr;
        }
    }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return raw_mem_; }

private:
    LPPROC_THREAD_ATTRIBUTE_LIST raw_mem_{nullptr};
    bool initialized_{false};
};

// ---------------------------------------------------------------------------
// Win32 String and Error Utilities
// ---------------------------------------------------------------------------

std::string format_win32_error(DWORD error_code) {
    if (error_code == 0) {
        return "Operation succeeded.";
    }
    LPWSTR buffer = nullptr;
    DWORD size = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    if (size == 0 || !buffer) {
        return "Windows error code " + std::to_string(error_code);
    }

    std::wstring ws(buffer, size);
    ::LocalFree(buffer);

    while (!ws.empty() && (ws.back() == L'\r' || ws.back() == L'\n')) {
        ws.pop_back();
    }

    int len = ::WideCharToMultiByte(
        CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return "Windows error code " + std::to_string(error_code);
    }
    std::string result(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), result.data(), len, nullptr, nullptr);
    return result;
}

std::optional<std::wstring> utf8_to_wide(std::string_view utf8_str) {
    if (utf8_str.empty()) {
        return std::wstring{};
    }
    int len = ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8_str.data(),
        static_cast<int>(utf8_str.size()),
        nullptr,
        0);
    if (len <= 0) {
        return std::nullopt;
    }
    std::wstring result(static_cast<size_t>(len), L'\0');
    if (::MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8_str.data(),
            static_cast<int>(utf8_str.size()),
            result.data(),
            len) <= 0) {
        return std::nullopt;
    }
    return result;
}

bool iequals(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (::towlower(a[i]) != ::towlower(b[i])) {
            return false;
        }
    }
    return true;
}

bool is_batch_extension(const std::filesystem::path& p) {
    auto ext = p.extension().wstring();
    return iequals(ext, L".cmd") || iequals(ext, L".bat");
}

// ---------------------------------------------------------------------------
// Thread Coordination State
// ---------------------------------------------------------------------------

struct ReaderContext {
    std::mutex mutex;
    char error_buffer[512]{};
    std::atomic<bool> has_fatal_error{false};
    HANDLE fatal_event{nullptr};

    void set_fatal_error(const char* msg) noexcept {
        has_fatal_error.store(true, std::memory_order_release);
        try {
            std::lock_guard<std::mutex> lock(mutex);
            if (msg) {
                std::strncpy(error_buffer, msg, sizeof(error_buffer) - 1);
                error_buffer[sizeof(error_buffer) - 1] = '\0';
            }
        } catch (...) {
            // Non-allocating fallback: atomic flag is already set
        }
        if (fatal_event) {
            ::SetEvent(fatal_event);
        }
    }
};

void read_stream(
    HANDLE pipe_read,
    std::string& out_content,
    bool& out_truncated,
    ReaderContext& context) noexcept {
    try {
        char buffer[4096];
        DWORD bytes_read = 0;
        size_t total_bytes = 0;
        constexpr size_t kLimit = WindowsProcessExecutor::kMaxStreamCaptureBytes;

        while (::ReadFile(pipe_read, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0) {
            total_bytes += bytes_read;
            if (out_content.size() < kLimit) {
                size_t available = kLimit - out_content.size();
                size_t to_append = std::min(static_cast<size_t>(bytes_read), available);
                try {
                    out_content.append(buffer, to_append);
                } catch (const std::bad_alloc&) {
                    context.set_fatal_error("Out of memory while buffering stream output.");
                    return;
                }
                if (total_bytes > kLimit) {
                    out_truncated = true;
                }
            } else {
                out_truncated = true;
            }
        }

        DWORD err = ::GetLastError();
        if (err != ERROR_BROKEN_PIPE && err != ERROR_HANDLE_EOF && err != NO_ERROR) {
            char err_buf[128];
            std::snprintf(err_buf, sizeof(err_buf), "ReadFile failed with OS error %lu", err);
            context.set_fatal_error(err_buf);
        }
    } catch (const std::exception& ex) {
        context.set_fatal_error(ex.what());
    } catch (...) {
        context.set_fatal_error("Unknown exception in stream reader thread.");
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Static Command-Line Quoting Implementation
// ---------------------------------------------------------------------------

std::wstring WindowsProcessExecutor::quote_argument(std::wstring_view arg) {
    if (arg.empty()) {
        return L"\"\"";
    }

    bool needs_quotes = false;
    for (wchar_t c : arg) {
        if (c == L' ' || c == L'\t' || c == L'\v' || c == L'\n' || c == L'"') {
            needs_quotes = true;
            break;
        }
    }

    if (!needs_quotes) {
        return std::wstring(arg);
    }

    std::wstring result;
    result.reserve(arg.size() + 16);
    result.push_back(L'"');

    size_t num_backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++num_backslashes;
        } else if (c == L'"') {
            // Under Microsoft CRT rules, 2N + 1 backslashes followed by quote
            result.append(2 * num_backslashes + 1, L'\\');
            result.push_back(L'"');
            num_backslashes = 0;
        } else {
            if (num_backslashes > 0) {
                result.append(num_backslashes, L'\\');
                num_backslashes = 0;
            }
            result.push_back(c);
        }
    }

    // Trailing backslashes before closing delimiter: 2N backslashes followed by plain quote
    if (num_backslashes > 0) {
        result.append(2 * num_backslashes, L'\\');
    }
    result.push_back(L'"');

    return result;
}

std::optional<std::wstring> WindowsProcessExecutor::build_command_line(
    std::wstring_view resolved_executable,
    const std::vector<std::string>& arguments,
    std::string& out_error_message) {

    std::wstring command_line = quote_argument(resolved_executable);

    for (const auto& arg : arguments) {
        auto wide_arg = utf8_to_wide(arg);
        if (!wide_arg.has_value()) {
            out_error_message = "Invalid UTF-8 byte sequence in command argument: " + arg;
            return std::nullopt;
        }
        command_line.push_back(L' ');
        command_line.append(quote_argument(*wide_arg));
    }

    if (command_line.size() >= kMaxCommandLineLength) {
        out_error_message = "Command line length (" + std::to_string(command_line.size()) +
            ") exceeds Windows maximum limit of 32767 characters.";
        return std::nullopt;
    }

    return command_line;
}

// ---------------------------------------------------------------------------
// Executable Resolution Implementation
// ---------------------------------------------------------------------------

std::optional<std::filesystem::path> WindowsProcessExecutor::resolve_executable(
    const std::filesystem::path& executable,
    unsigned long& out_error_code,
    std::string& out_error_message) {

    out_error_code = 0;
    out_error_message.clear();

    if (executable.empty()) {
        out_error_code = ERROR_FILE_NOT_FOUND;
        out_error_message = "Executable path is empty.";
        return std::nullopt;
    }

    std::wstring native_str = executable.native();
    bool has_separators = (native_str.find(L'/') != std::wstring::npos ||
                           native_str.find(L'\\') != std::wstring::npos);

    // 1. Explicit Path
    if (has_separators) {
        if (is_batch_extension(executable)) {
            out_error_code = ERROR_BAD_EXE_FORMAT;
            out_error_message = "Batch scripts (.cmd, .bat) are not supported directly. "
                                "Invoke cmd.exe explicitly if shell execution is required.";
            return std::nullopt;
        }

        std::error_code ec;
        auto abs_path = std::filesystem::absolute(executable, ec);
        if (ec) {
            out_error_code = static_cast<unsigned long>(ec.value());
            out_error_message = "Failed to resolve absolute path for '" + manifest::path_to_utf8(executable) + "': " + ec.message();
            return std::nullopt;
        }

        if (!std::filesystem::exists(abs_path, ec) || !std::filesystem::is_regular_file(abs_path, ec)) {
            out_error_code = ERROR_FILE_NOT_FOUND;
            out_error_message = "Executable not found at path: " + manifest::path_to_utf8(abs_path);
            return std::nullopt;
        }

        if (is_batch_extension(abs_path)) {
            out_error_code = ERROR_BAD_EXE_FORMAT;
            out_error_message = "Batch scripts (.cmd, .bat) are not supported directly.";
            return std::nullopt;
        }

        return abs_path;
    }

    // 2. Bare Executable Name
    if (is_batch_extension(executable)) {
        out_error_code = ERROR_BAD_EXE_FORMAT;
        out_error_message = "Batch scripts (.cmd, .bat) are not supported directly. "
                            "Invoke cmd.exe explicitly if shell execution is required.";
        return std::nullopt;
    }

    const wchar_t* path_env = _wgetenv(L"PATH");
    if (!path_env || *path_env == L'\0') {
        out_error_code = ERROR_FILE_NOT_FOUND;
        out_error_message = "PATH environment variable is empty or not set.";
        return std::nullopt;
    }

    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);

    std::vector<std::filesystem::path> path_dirs;
    std::wstring path_env_str(path_env);
    size_t start = 0;
    while (start < path_env_str.size()) {
        size_t end = path_env_str.find(L';', start);
        if (end == std::wstring::npos) {
            end = path_env_str.size();
        }
        std::wstring token = path_env_str.substr(start, end - start);
        start = end + 1;

        // Trim whitespace
        while (!token.empty() && (token.front() == L' ' || token.front() == L'\t')) {
            token.erase(token.begin());
        }
        while (!token.empty() && (token.back() == L' ' || token.back() == L'\t')) {
            token.pop_back();
        }

        if (token.empty()) {
            continue;
        }

        std::filesystem::path dir(token);
        auto abs_dir = std::filesystem::absolute(dir, ec);
        if (ec) {
            continue;
        }

        // Exclude current working directory
        if (!ec && !cwd.empty() && std::filesystem::equivalent(abs_dir, cwd, ec)) {
            continue;
        }

        if (std::filesystem::is_directory(abs_dir, ec)) {
            path_dirs.push_back(std::move(abs_dir));
        }
    }

    bool has_exe_ext = iequals(executable.extension().wstring(), L".exe");
    bool found_batch_script = false;
    std::filesystem::path batch_script_path;

    // Search for .exe candidates across sanitized PATH directories in order
    for (const auto& dir : path_dirs) {
        if (has_exe_ext) {
            auto candidate = dir / executable;
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
                return candidate;
            }
        } else {
            auto exe_candidate = dir / (executable.wstring() + L".exe");
            if (std::filesystem::exists(exe_candidate, ec) && std::filesystem::is_regular_file(exe_candidate, ec)) {
                return exe_candidate;
            }

            if (!found_batch_script) {
                auto cmd_cand = dir / (executable.wstring() + L".cmd");
                auto bat_cand = dir / (executable.wstring() + L".bat");
                if (std::filesystem::exists(cmd_cand, ec) && std::filesystem::is_regular_file(cmd_cand, ec)) {
                    found_batch_script = true;
                    batch_script_path = cmd_cand;
                } else if (std::filesystem::exists(bat_cand, ec) && std::filesystem::is_regular_file(bat_cand, ec)) {
                    found_batch_script = true;
                    batch_script_path = bat_cand;
                }
            }
        }
    }

    if (found_batch_script) {
        out_error_code = ERROR_BAD_EXE_FORMAT;
        out_error_message = "Found batch script '" + manifest::path_to_utf8(batch_script_path) +
            "' in PATH, but batch scripts are not supported directly.";
        return std::nullopt;
    }

    out_error_code = ERROR_FILE_NOT_FOUND;
    out_error_message = "Executable '" + manifest::path_to_utf8(executable) + "' was not found in environment PATH.";
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Execution Entry Point
// ---------------------------------------------------------------------------

scheduler::ExecutionResponse WindowsProcessExecutor::execute(
    const scheduler::ExecutionRequest& request) {

    auto start_time = std::chrono::steady_clock::now();

    // 1. Resolve executable
    unsigned long res_err_code = 0;
    std::string res_err_msg;
    auto resolved_path_opt = resolve_executable(request.executable, res_err_code, res_err_msg);
    if (!resolved_path_opt.has_value()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::launch_failure(res_err_code, res_err_msg, elapsed);
    }
    const auto& resolved_path = *resolved_path_opt;

    // 2. Build command line
    std::string cmd_err_msg;
    auto cmd_line_opt = build_command_line(resolved_path.wstring(), request.arguments, cmd_err_msg);
    if (!cmd_line_opt.has_value()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::launch_failure(
            ERROR_BUFFER_OVERFLOW, cmd_err_msg, elapsed);
    }
    std::wstring cmd_line = *cmd_line_opt;
    std::vector<wchar_t> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back(L'\0');

    // 3. Resolve working directory if specified
    std::optional<std::wstring> wd_wide;
    if (request.working_directory.has_value()) {
        std::error_code ec;
        auto abs_wd = std::filesystem::absolute(*request.working_directory, ec);
        if (ec || !std::filesystem::is_directory(abs_wd, ec)) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return scheduler::ExecutionResponse::launch_failure(
                ERROR_DIRECTORY,
                "Working directory does not exist or is not a directory: " +
                    manifest::path_to_utf8(*request.working_directory),
                elapsed);
        }
        wd_wide = abs_wd.wstring();
    }

    // 4. Create Standard Input directed to NUL
    SECURITY_ATTRIBUTES saInherit{};
    saInherit.nLength = sizeof(SECURITY_ATTRIBUTES);
    saInherit.bInheritHandle = TRUE;
    saInherit.lpSecurityDescriptor = nullptr;

    AutoHandle hStdInRead(::CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &saInherit,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));

    if (!hStdInRead.is_valid()) {
        DWORD err = ::GetLastError();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::system_fault(
            "Failed to open NUL device for stdin: " + format_win32_error(err), elapsed);
    }

    // 5. Create Anonymous Output Pipes
    HANDLE hStdoutReadRaw = nullptr;
    HANDLE hStdoutWriteRaw = nullptr;
    if (!::CreatePipe(&hStdoutReadRaw, &hStdoutWriteRaw, &saInherit, 0)) {
        DWORD err = ::GetLastError();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::system_fault(
            "Failed to create stdout pipe: " + format_win32_error(err), elapsed);
    }
    AutoHandle hStdoutRead(hStdoutReadRaw);
    AutoHandle hStdoutWrite(hStdoutWriteRaw);

    HANDLE hStderrReadRaw = nullptr;
    HANDLE hStderrWriteRaw = nullptr;
    if (!::CreatePipe(&hStderrReadRaw, &hStderrWriteRaw, &saInherit, 0)) {
        DWORD err = ::GetLastError();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::system_fault(
            "Failed to create stderr pipe: " + format_win32_error(err), elapsed);
    }
    AutoHandle hStderrRead(hStderrReadRaw);
    AutoHandle hStderrWrite(hStderrWriteRaw);

    // Ensure read handles are strictly non-inheritable
    ::SetHandleInformation(hStdoutRead.get(), HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(hStderrRead.get(), HANDLE_FLAG_INHERIT, 0);

    // 6. Setup STARTUPINFOEXW with Whitelisted Handle List
    ProcThreadAttributeGuard attr_guard;
    if (!attr_guard.initialize(1)) {
        DWORD err = ::GetLastError();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::system_fault(
            "Failed to initialize proc thread attribute list: " + format_win32_error(err), elapsed);
    }

    HANDLE inherited_handles[3] = {
        hStdInRead.get(),
        hStdoutWrite.get(),
        hStderrWrite.get()
    };

    if (!::UpdateProcThreadAttribute(
            attr_guard.get(),
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited_handles,
            sizeof(inherited_handles),
            nullptr,
            nullptr)) {
        DWORD err = ::GetLastError();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::system_fault(
            "Failed to update proc thread attribute list: " + format_win32_error(err), elapsed);
    }

    STARTUPINFOEXW siEx{};
    siEx.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    siEx.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    siEx.StartupInfo.hStdInput = inherited_handles[0];
    siEx.StartupInfo.hStdOutput = inherited_handles[1];
    siEx.StartupInfo.hStdError = inherited_handles[2];
    siEx.lpAttributeList = attr_guard.get();

    // 7. Setup Fatal Event and Pre-Launch Reader Threads
    AutoHandle hFatalEvent(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!hFatalEvent.is_valid()) {
        DWORD err = ::GetLastError();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::system_fault(
            "Failed to create fatal error event: " + format_win32_error(err), elapsed);
    }

    ReaderContext context;
    context.fatal_event = hFatalEvent.get();

    std::string stdout_content;
    bool stdout_truncated = false;
    std::string stderr_content;
    bool stderr_truncated = false;

    std::thread stdout_thread;
    std::thread stderr_thread;

    try {
        stdout_thread = std::thread([&]() noexcept {
            read_stream(hStdoutRead.get(), stdout_content, stdout_truncated, context);
        });
    } catch (const std::system_error& ex) {
        hStdoutWrite.reset();
        hStderrWrite.reset();
        hStdInRead.reset();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::system_fault(
            "Failed to spawn stdout reader thread: " + std::string(ex.what()), elapsed);
    }

    try {
        stderr_thread = std::thread([&]() noexcept {
            read_stream(hStderrRead.get(), stderr_content, stderr_truncated, context);
        });
    } catch (const std::system_error& ex) {
        hStdoutWrite.reset();
        hStderrWrite.reset();
        hStdInRead.reset();
        if (stdout_thread.joinable()) {
            stdout_thread.join();
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::system_fault(
            "Failed to spawn stderr reader thread: " + std::string(ex.what()), elapsed);
    }

    // 8. Launch Process
    PROCESS_INFORMATION pi{};
    BOOL success = ::CreateProcessW(
        resolved_path.c_str(),
        cmd_buf.data(),
        nullptr,
        nullptr,
        TRUE,
        EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        wd_wide.has_value() ? wd_wide->c_str() : nullptr,
        &siEx.StartupInfo,
        &pi);

    if (!success) {
        DWORD launch_err = ::GetLastError();
        // Immediately close parent write ends so reader threads unblock on EOF
        hStdoutWrite.reset();
        hStderrWrite.reset();
        hStdInRead.reset();

        if (stdout_thread.joinable()) stdout_thread.join();
        if (stderr_thread.joinable()) stderr_thread.join();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::launch_failure(
            launch_err, format_win32_error(launch_err), elapsed);
    }

    AutoHandle hProcess(pi.hProcess);
    AutoHandle hThread(pi.hThread);
    hThread.reset(); // Primary thread handle not needed

    // 9. Immediately Close Parent-Side Write Handles
    hStdoutWrite.reset();
    hStderrWrite.reset();
    hStdInRead.reset();

    // 10. Wait for Process Exit or Fatal Reader Error
    HANDLE wait_handles[2] = { hProcess.get(), hFatalEvent.get() };
    DWORD wait_res = ::WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);

    if (wait_res == WAIT_OBJECT_0 + 1) {
        // Fatal reader error occurred while process was running!
        ::TerminateProcess(hProcess.get(), 1);
        ::WaitForSingleObject(hProcess.get(), 5000);

        if (stdout_thread.joinable()) stdout_thread.join();
        if (stderr_thread.joinable()) stderr_thread.join();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::system_fault(
            "Fatal pipe reader error: " + std::string(context.error_buffer), elapsed);
    } else if (wait_res == WAIT_OBJECT_0) {
        // Process finished normally, wait for readers to finish draining
        if (stdout_thread.joinable()) stdout_thread.join();
        if (stderr_thread.joinable()) stderr_thread.join();

        if (context.has_fatal_error.load(std::memory_order_acquire)) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return scheduler::ExecutionResponse::system_fault(
                "Fatal pipe reader error: " + std::string(context.error_buffer), elapsed);
        }

        if (stdout_truncated) {
            stdout_content.append("\n[TaskForge: output truncated after exceeding 10 MiB limit]");
        }
        if (stderr_truncated) {
            stderr_content.append("\n[TaskForge: output truncated after exceeding 10 MiB limit]");
        }

        DWORD exit_code = 0;
        if (!::GetExitCodeProcess(hProcess.get(), &exit_code)) {
            DWORD err = ::GetLastError();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return scheduler::ExecutionResponse::system_fault(
                "Failed to get process exit code: " + format_win32_error(err), elapsed);
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        return scheduler::ExecutionResponse::completed(
            static_cast<int>(exit_code),
            std::move(stdout_content),
            std::move(stderr_content),
            elapsed);
    } else {
        // Unexpected wait return (WAIT_FAILED)
        DWORD err = ::GetLastError();
        ::TerminateProcess(hProcess.get(), 1);
        ::WaitForSingleObject(hProcess.get(), 5000);

        if (stdout_thread.joinable()) stdout_thread.join();
        if (stderr_thread.joinable()) stderr_thread.join();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return scheduler::ExecutionResponse::system_fault(
            "WaitForMultipleObjects failed: " + format_win32_error(err), elapsed);
    }
}

} // namespace taskforge::platform::win32

#endif // defined(_WIN32)
