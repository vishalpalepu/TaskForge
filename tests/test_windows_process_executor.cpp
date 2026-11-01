#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
#include "taskforge/platform/win32/windows_process_executor.hpp"
#include "taskforge/manifest/manifest_parser.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace taskforge::platform::win32;
using namespace taskforge::scheduler;

namespace {

std::filesystem::path get_test_helper_path() {
#if defined(TASKFORGE_TEST_HELPER_PATH)
    return taskforge::manifest::utf8_to_path(TASKFORGE_TEST_HELPER_PATH);
#else
    return std::filesystem::path("taskforge_test_helper.exe");
#endif
}

} // namespace

TEST_CASE("WindowsProcessExecutor: Unit Tests for Quoting and Resolution", "[platform][win32]") {
    SECTION("Command-line argument quoting targeting Microsoft UCRT") {
        // 1. Empty argument
        CHECK(WindowsProcessExecutor::quote_argument(L"") == L"\"\"");

        // 2. Simple arguments without spaces or quotes require no quotes
        CHECK(WindowsProcessExecutor::quote_argument(L"hello") == L"hello");
        CHECK(WindowsProcessExecutor::quote_argument(L"C:\\path\\file.txt") == L"C:\\path\\file.txt");

        // 3. Arguments containing spaces or tabs require quotes
        CHECK(WindowsProcessExecutor::quote_argument(L"hello world") == L"\"hello world\"");
        CHECK(WindowsProcessExecutor::quote_argument(L"arg\twith\ttab") == L"\"arg\twith\ttab\"");

        // 4. Arguments containing literal quotes require 2N + 1 backslashes
        CHECK(WindowsProcessExecutor::quote_argument(L"quote:\"escaped\"") == L"\"quote:\\\"escaped\\\"\"");

        // 5. Backslashes immediately preceding a quote are doubled + 1
        CHECK(WindowsProcessExecutor::quote_argument(L"C:\\path\\\"quote\"") == L"\"C:\\path\\\\\\\"quote\\\"\"");

        // 6. Trailing backslashes inside a quoted argument are doubled before plain quote
        CHECK(WindowsProcessExecutor::quote_argument(L"C:\\Program Files\\") == L"\"C:\\Program Files\\\\\"");
        CHECK(WindowsProcessExecutor::quote_argument(L"dir with space\\\\") == L"\"dir with space\\\\\\\\\"");

        // 7. Unicode characters preserved
        CHECK(WindowsProcessExecutor::quote_argument(L"тест äöü 日本語") == L"\"тест äöü 日本語\"");
    }

    SECTION("Command-line construction and buffer limit validation") {
        std::string err;
        auto cmd = WindowsProcessExecutor::build_command_line(
            L"C:\\Tools\\app.exe", {"--flag", "value with space"}, err);
        REQUIRE(cmd.has_value());
        CHECK(*cmd == L"C:\\Tools\\app.exe --flag \"value with space\"");

        // Invalid UTF-8 sequence rejection
        auto invalid_cmd = WindowsProcessExecutor::build_command_line(
            L"app.exe", {"\xFF\xFE"}, err);
        CHECK_FALSE(invalid_cmd.has_value());
        CHECK(err.find("Invalid UTF-8") != std::string::npos);

        // Limit rejection (> 32767 chars)
        std::string huge_arg(32768, 'A');
        auto overflow_cmd = WindowsProcessExecutor::build_command_line(
            L"app.exe", {huge_arg}, err);
        CHECK_FALSE(overflow_cmd.has_value());
        CHECK(err.find("exceeds Windows maximum limit") != std::string::npos);
    }

    SECTION("Executable resolution and extension policies") {
        unsigned long err_code = 0;
        std::string err_msg;

        // 1. Missing executable
        auto missing = WindowsProcessExecutor::resolve_executable(
            "non_existent_binary_xyz_12345.exe", err_code, err_msg);
        CHECK_FALSE(missing.has_value());
        CHECK(err_code == ERROR_FILE_NOT_FOUND);

        // 2. Explicit path to .cmd or .bat rejected
        auto batch_cmd = WindowsProcessExecutor::resolve_executable(
            "C:/scripts/deploy.cmd", err_code, err_msg);
        CHECK_FALSE(batch_cmd.has_value());
        CHECK(err_code == ERROR_BAD_EXE_FORMAT);
        CHECK(err_msg.find("Batch scripts (.cmd, .bat) are not supported") != std::string::npos);

        auto batch_bat = WindowsProcessExecutor::resolve_executable(
            "./run_job.BAT", err_code, err_msg);
        CHECK_FALSE(batch_bat.has_value());
        CHECK(err_code == ERROR_BAD_EXE_FORMAT);

        // 3. Bare name ending in .cmd or .bat rejected
        auto bare_batch = WindowsProcessExecutor::resolve_executable(
            "build.bat", err_code, err_msg);
        CHECK_FALSE(bare_batch.has_value());
        CHECK(err_code == ERROR_BAD_EXE_FORMAT);

        // 4. Valid explicit path to helper executable
        auto helper_path = get_test_helper_path();
        REQUIRE(std::filesystem::exists(helper_path));
        auto resolved_helper = WindowsProcessExecutor::resolve_executable(
            helper_path, err_code, err_msg);
        REQUIRE(resolved_helper.has_value());
        CHECK(std::filesystem::equivalent(*resolved_helper, helper_path));
    }
}

TEST_CASE("WindowsProcessExecutor: End-to-End Execution with Test Helper", "[platform][win32]") {
    WindowsProcessExecutor executor;
    auto helper = get_test_helper_path();
    REQUIRE(std::filesystem::exists(helper));

    SECTION("Exit code 0 reports successful completion") {
        ExecutionRequest req{
            .executable = helper,
            .arguments = {"--exit", "0"}
        };
        auto resp = executor.execute(req);
        CHECK(resp.is_successful());
        CHECK(resp.exit_code() == 0);
        CHECK_FALSE(resp.is_launch_failure());
        CHECK_FALSE(resp.is_system_fault());
    }

    SECTION("Non-zero exit codes (1, 42) report exit code accurately") {
        ExecutionRequest req{
            .executable = helper,
            .arguments = {"--exit", "42"}
        };
        auto resp = executor.execute(req);
        CHECK_FALSE(resp.is_successful());
        CHECK(resp.exit_code() == 42);
        CHECK_FALSE(resp.is_launch_failure());
        CHECK_FALSE(resp.is_system_fault());
    }

    SECTION("Standard output capture") {
        ExecutionRequest req{
            .executable = helper,
            .arguments = {"--stdout", "TaskForge stdout test output!"}
        };
        auto resp = executor.execute(req);
        CHECK(resp.is_successful());
        CHECK(resp.standard_output() == "TaskForge stdout test output!");
        CHECK(resp.standard_error().empty());
    }

    SECTION("Standard error capture") {
        ExecutionRequest req{
            .executable = helper,
            .arguments = {"--stderr", "TaskForge stderr test error!"}
        };
        auto resp = executor.execute(req);
        CHECK(resp.is_successful());
        CHECK(resp.standard_output().empty());
        CHECK(resp.standard_error() == "TaskForge stderr test error!");
    }

    SECTION("Standard input connected to NUL receives EOF immediately") {
        ExecutionRequest req{
            .executable = helper,
            .arguments = {"--read-stdin"}
        };
        auto resp = executor.execute(req);
        CHECK(resp.is_successful());
        CHECK(resp.standard_output() == "STDIN_EOF");
    }

    SECTION("Working directory configuration") {
        auto temp_dir = std::filesystem::temp_directory_path();
        ExecutionRequest req{
            .executable = helper,
            .arguments = {"--pwd"},
            .working_directory = temp_dir
        };
        auto resp = executor.execute(req);
        CHECK(resp.is_successful());
        auto reported_path = taskforge::manifest::utf8_to_path(resp.standard_output());
        std::error_code ec;
        CHECK(std::filesystem::equivalent(reported_path, temp_dir, ec));
    }

    SECTION("UCRT Argument Quoting Round-Trip Verification via --echo-args") {
        std::vector<std::string> test_args = {
            "--echo-args",
            "",                             // Empty argument
            "simple",                       // Unquoted
            "with spaces",                  // Spaces
            "with\ttabs",                   // Tab character
            "quote:\"val\"",                // Embedded quotes
            "C:\\path\\\"with_quote\"",     // Backslashes before quote
            "C:\\Program Files\\",          // Trailing backslash with spaces
            "dir\\",                        // Trailing backslash without spaces
            "тест äöü 日本語"              // Non-ASCII Unicode characters
        };

        ExecutionRequest req{
            .executable = helper,
            .arguments = test_args
        };

        auto resp = executor.execute(req);
        REQUIRE(resp.is_successful());

        // Verify each echoed argument matches exactly
        std::istringstream stream(resp.standard_output());
        std::string line;
        for (size_t i = 1; i < test_args.size(); ++i) {
            REQUIRE(std::getline(stream, line));
            std::string expected_prefix = "[" + std::to_string(i - 1) + "]: ";
            REQUIRE(line.rfind(expected_prefix, 0) == 0);
            std::string actual_arg = line.substr(expected_prefix.size());
            CHECK(actual_arg == test_args[i]);
        }
    }

    SECTION("Interleaved large output (> 1 MiB total) does not deadlock") {
        // 1 MiB to stdout and 1 MiB to stderr written in 4 KiB alternating chunks
        // OS pipe buffers are 64 KiB, so sequential readers would deadlock immediately
        constexpr long long kTotalBytes = 1048576;
        constexpr long long kChunkSize = 4096;

        ExecutionRequest req{
            .executable = helper,
            .arguments = {
                "--stream-interleaved",
                std::to_string(kTotalBytes),
                std::to_string(kChunkSize)
            }
        };

        auto resp = executor.execute(req);
        REQUIRE(resp.is_successful());
        CHECK(resp.standard_output().size() == static_cast<size_t>(kTotalBytes));
        CHECK(resp.standard_error().size() == static_cast<size_t>(kTotalBytes));
    }

    SECTION("Output truncation at 10 MiB limit continues draining") {
        // Write 12 MiB to stdout; executor caps capture at 10 MiB and appends notice
        constexpr long long kTotalBytes = 12582912; // 12 MiB

        ExecutionRequest req{
            .executable = helper,
            .arguments = {
                "--stream-large-stdout",
                std::to_string(kTotalBytes)
            }
        };

        auto resp = executor.execute(req);
        REQUIRE(resp.is_successful());
        CHECK(resp.standard_output().find("[TaskForge: output truncated after exceeding 10 MiB limit]") != std::string::npos);
        // Captured size should be capped at 10 MiB + truncation marker length
        CHECK(resp.standard_output().size() <= WindowsProcessExecutor::kMaxStreamCaptureBytes + 128);
    }

    SECTION("Launch failure when executable does not exist") {
        ExecutionRequest req{
            .executable = "non_existent_binary_xyz_98765.exe",
            .arguments = {}
        };
        auto resp = executor.execute(req);
        CHECK_FALSE(resp.is_successful());
        CHECK(resp.is_launch_failure());
        CHECK(resp.os_error_code() == ERROR_FILE_NOT_FOUND);
        CHECK_FALSE(resp.exit_code().has_value());
    }
}

#endif // defined(_WIN32)
