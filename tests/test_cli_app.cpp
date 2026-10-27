#include "taskforge/cli/cli_app.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

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
}
