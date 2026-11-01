#include "taskforge/cli/cli_app.hpp"
#include "taskforge/manifest/manifest_parser.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace taskforge::cli;

namespace {

struct TempManifestFile {
    std::filesystem::path path;

    explicit TempManifestFile(std::string_view content, std::string_view filename = "test_manifest.json") {
        path = std::filesystem::temp_directory_path() / filename;
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    ~TempManifestFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

} // namespace

TEST_CASE("CliApp: Command-line dispatching and exit codes", "[cli]") {
    std::ostringstream out;
    std::ostringstream err;
    CliApp app(out, err);

    SECTION("No arguments reports error and usage with exit code 1") {
        int code = app.run({});
        CHECK(code == static_cast<int>(ExitCode::InvalidUsage));
        CHECK(err.str().find("No command specified") != std::string::npos);
    }

    SECTION("Help flag returns exit code 0 and prints usage") {
        int code = app.run({"--help"});
        CHECK(code == static_cast<int>(ExitCode::Success));
        CHECK(out.str().find("TaskForge") != std::string::npos);
        CHECK(out.str().find("validate") != std::string::npos);
        CHECK(out.str().find("run") != std::string::npos);
    }

    SECTION("Unrecognized command reports error with exit code 1") {
        int code = app.run({"foobar"});
        CHECK(code == static_cast<int>(ExitCode::InvalidUsage));
        CHECK(err.str().find("Unrecognized command 'foobar'") != std::string::npos);
    }

    SECTION("validate without file argument reports error with exit code 1") {
        int code = app.run({"validate"});
        CHECK(code == static_cast<int>(ExitCode::InvalidUsage));
        CHECK(err.str().find("requires exactly one manifest file") != std::string::npos);
    }

    SECTION("validate with extra arguments reports error with exit code 1") {
        int code = app.run({"validate", "file1.json", "file2.json"});
        CHECK(code == static_cast<int>(ExitCode::InvalidUsage));
        CHECK(err.str().find("requires exactly one manifest file") != std::string::npos);
    }

    SECTION("validate with non-existent file returns exit code 2 (FileNotFound)") {
        int code = app.run({"validate", "non_existent_manifest_12345.json"});
        CHECK(code == static_cast<int>(ExitCode::FileNotFound));
        CHECK(err.str().find("Cannot open manifest file") != std::string::npos);
    }

    SECTION("validate with valid manifest prints summary and returns exit code 0") {
        std::string json = R"({
            "name": "cli_valid_wf",
            "tasks": [
                { "id": "t1", "executable": "echo.exe" },
                { "id": "t2", "executable": "echo.exe", "dependencies": ["t1"] }
            ]
        })";
        TempManifestFile temp_file(json, "valid_manifest_test.json");

        int code = app.run({"validate", temp_file.path.string()});
        CHECK(code == static_cast<int>(ExitCode::Success));
        CHECK(out.str().find("Workflow 'cli_valid_wf' is valid (2 tasks).") != std::string::npos);
        CHECK(err.str().empty());
    }

    SECTION("validate with invalid manifest prints diagnostics to stderr and returns exit code 3") {
        // Contains circular dependency: t1 -> t2 -> t1
        std::string json = R"({
            "name": "cli_invalid_wf",
            "tasks": [
                { "id": "t1", "executable": "echo.exe", "dependencies": ["t2"] },
                { "id": "t2", "executable": "echo.exe", "dependencies": ["t1"] }
            ]
        })";
        TempManifestFile temp_file(json, "invalid_manifest_test.json");

        int code = app.run({"validate", temp_file.path.string()});
        CHECK(code == static_cast<int>(ExitCode::ValidationFailed));
        CHECK(out.str().empty());
        CHECK(err.str().find("CIRCULAR_DEPENDENCY") != std::string::npos);
    }

    SECTION("validate with non-ASCII directory and filename in path succeeds with exit code 0") {
        auto temp_root = std::filesystem::temp_directory_path();
        auto unicode_dir = temp_root / taskforge::manifest::utf8_to_path("taskforge_cli_тест_äöü");
        std::error_code ec;
        std::filesystem::create_directories(unicode_dir, ec);
        REQUIRE_FALSE(ec);

        auto manifest_file = unicode_dir / taskforge::manifest::utf8_to_path("манифест.json");
        std::string json = R"({
            "name": "unicode_cli_wf",
            "tasks": [
                { "id": "t1", "executable": "echo.exe" }
            ]
        })";

        {
            std::ofstream file(manifest_file, std::ios::binary);
            REQUIRE(file.is_open());
            file << json;
        }

        std::string utf8_path_str = taskforge::manifest::path_to_utf8(manifest_file);
        int code = app.run({"validate", utf8_path_str});

        CHECK(code == static_cast<int>(ExitCode::Success));
        CHECK(out.str().find("Workflow 'unicode_cli_wf' is valid (1 task).") != std::string::npos);
        CHECK(err.str().empty());

        std::filesystem::remove(manifest_file, ec);
        std::filesystem::remove(unicode_dir, ec);
    }

    SECTION("End-to-end process launch: taskforge.exe validate with Unicode manifest path") {
#if defined(_WIN32) && defined(TASKFORGE_EXE_PATH)
        auto temp_root = std::filesystem::temp_directory_path();
        auto unicode_dir = temp_root / taskforge::manifest::utf8_to_path("taskforge_cli_e2e_тест_äöü");
        std::error_code ec;
        std::filesystem::create_directories(unicode_dir, ec);
        REQUIRE_FALSE(ec);

        auto manifest_file = unicode_dir / taskforge::manifest::utf8_to_path("манифест_e2e.json");
        std::string json = R"({
            "name": "unicode_e2e_cli_wf",
            "tasks": [
                { "id": "t1", "executable": "echo.exe" }
            ]
        })";

        {
            std::ofstream file(manifest_file, std::ios::binary);
            REQUIRE(file.is_open());
            file << json;
        }

        std::filesystem::path exe_path = taskforge::manifest::utf8_to_path(TASKFORGE_EXE_PATH);
        REQUIRE(std::filesystem::exists(exe_path));

        // Construct wide command line: "exe_path" validate "manifest_file"
        std::wstring cmd = L"\"" + exe_path.wstring() + L"\" validate \"" + manifest_file.wstring() + L"\"";
        std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back(L'\0');

        HANDLE stdout_read = NULL;
        HANDLE stdout_write = NULL;
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        REQUIRE(::CreatePipe(&stdout_read, &stdout_write, &sa, 0));
        ::SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(STARTUPINFOW);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = stdout_write;
        si.hStdError = stdout_write;
        si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION pi{};
        BOOL success = ::CreateProcessW(
            exe_path.c_str(),
            cmd_buf.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        ::CloseHandle(stdout_write); // Close write end in parent so ReadFile terminates

        REQUIRE(success);

        std::string captured_output;
        char buffer[512];
        DWORD bytes_read = 0;
        while (::ReadFile(stdout_read, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0) {
            captured_output.append(buffer, bytes_read);
        }
        ::CloseHandle(stdout_read);

        ::WaitForSingleObject(pi.hProcess, 5000);
        DWORD exit_code = 0;
        ::GetExitCodeProcess(pi.hProcess, &exit_code);

        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);

        std::filesystem::remove(manifest_file, ec);
        std::filesystem::remove(unicode_dir, ec);

        CHECK(exit_code == 0);
        CHECK(captured_output.find("Workflow 'unicode_e2e_cli_wf' is valid (1 task).") != std::string::npos);
#endif
    }

    SECTION("run without arguments reports error with exit code 1") {
        int code = app.run({"run"});
        CHECK(code == static_cast<int>(ExitCode::InvalidUsage));
        CHECK(err.str().find("requires exactly one manifest file") != std::string::npos);
    }

    SECTION("run with non-existent file returns exit code 2") {
        int code = app.run({"run", "missing_file_xyz.json"});
        CHECK(code == static_cast<int>(ExitCode::FileNotFound));
        CHECK(err.str().find("Cannot open manifest file") != std::string::npos);
    }

    SECTION("run with invalid manifest returns exit code 3") {
        std::string json = R"({
            "name": "invalid_wf",
            "tasks": [
                { "id": "t1", "executable": "echo.exe", "dependencies": ["t1"] }
            ]
        })";
        TempManifestFile temp_file(json, "invalid_wf_test.json");
        int code = app.run({"run", temp_file.path.string()});
        CHECK(code == static_cast<int>(ExitCode::ValidationFailed));
    }

#if defined(_WIN32) && defined(TASKFORGE_TEST_HELPER_PATH)
    SECTION("run with valid workflow executing test helper succeeds with exit code 0") {
        std::string helper_u8 = TASKFORGE_TEST_HELPER_PATH;
        std::replace(helper_u8.begin(), helper_u8.end(), '\\', '/');
        std::string json = "{\n"
            "  \"name\": \"cli_run_success\",\n"
            "  \"tasks\": [\n"
            "    { \"id\": \"t1\", \"executable\": \"" + helper_u8 + "\", \"arguments\": [\"--stdout\", \"Hello from t1\"] },\n"
            "    { \"id\": \"t2\", \"executable\": \"" + helper_u8 + "\", \"arguments\": [\"--exit\", \"0\"], \"dependencies\": [\"t1\"] }\n"
            "  ]\n"
            "}";

        TempManifestFile temp_file(json, "cli_run_success.json");
        int code = app.run({"run", temp_file.path.string()});
        CHECK(code == static_cast<int>(ExitCode::Success));
        CHECK(out.str().find("[Succeeded] t1") != std::string::npos);
        CHECK(out.str().find("[Succeeded] t2") != std::string::npos);
        CHECK(out.str().find("Workflow execution finished: 2 succeeded, 0 failed, 0 blocked.") != std::string::npos);
    }

    SECTION("run with failing task blocks downstream dependent and preserves independent branch (exit code 4)") {
        std::string helper_u8 = TASKFORGE_TEST_HELPER_PATH;
        std::replace(helper_u8.begin(), helper_u8.end(), '\\', '/');
        std::string json = "{\n"
            "  \"name\": \"cli_run_failure\",\n"
            "  \"tasks\": [\n"
            "    { \"id\": \"fail_task\", \"executable\": \"" + helper_u8 + "\", \"arguments\": [\"--exit\", \"42\"] },\n"
            "    { \"id\": \"blocked_task\", \"executable\": \"" + helper_u8 + "\", \"dependencies\": [\"fail_task\"] },\n"
            "    { \"id\": \"independent_task\", \"executable\": \"" + helper_u8 + "\", \"arguments\": [\"--exit\", \"0\"] }\n"
            "  ]\n"
            "}";

        TempManifestFile temp_file(json, "cli_run_failure.json");
        int code = app.run({"run", temp_file.path.string()});
        CHECK(code == static_cast<int>(ExitCode::ExecutionFailed));
        CHECK(out.str().find("[Failed] fail_task") != std::string::npos);
        CHECK(out.str().find("[Blocked] blocked_task") != std::string::npos);
        CHECK(out.str().find("[Succeeded] independent_task") != std::string::npos);
        CHECK(out.str().find("Workflow execution finished: 1 succeeded, 1 failed, 1 blocked.") != std::string::npos);
    }
#endif
}
