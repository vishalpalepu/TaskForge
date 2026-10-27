#include "taskforge/manifest/manifest_parser.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fstream>

using namespace taskforge::core;
using namespace taskforge::manifest;

TEST_CASE("WorkflowManifestParser: Valid manifest parsing", "[manifest]") {
    SECTION("Complete valid manifest with all supported fields") {
        std::string json = R"({
            "version": "1.0",
            "name": "build-and-test",
            "tasks": [
                {
                    "id": "compile",
                    "description": "Compile C++ engine",
                    "executable": "cmake",
                    "arguments": ["--build", "build", "--config", "Release"],
                    "working_directory": "C:/Projects/Engine",
                    "dependencies": []
                },
                {
                    "id": "unit_tests",
                    "description": "Run Catch2 test runner",
                    "executable": "ctest",
                    "arguments": ["--test-dir", "build", "--output-on-failure"],
                    "working_directory": null,
                    "dependencies": ["compile"]
                }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);

        REQUIRE(result.is_valid());
        CHECK(static_cast<bool>(result));
        CHECK_THROWS_AS(result.diagnostics(), std::logic_error);

        const auto& wf = result.workflow();
        CHECK(wf.name() == "build-and-test");
        REQUIRE(wf.task_count() == 2);

        const auto* t1 = wf.find_task(TaskId("compile"));
        REQUIRE(t1 != nullptr);
        CHECK(t1->description() == "Compile C++ engine");
        CHECK(t1->executable() == utf8_to_path("cmake"));
        CHECK(t1->arguments() == std::vector<std::string>{"--build", "build", "--config", "Release"});
        REQUIRE(t1->working_directory().has_value());
        CHECK(*t1->working_directory() == utf8_to_path("C:/Projects/Engine"));
        CHECK(t1->dependencies().empty());

        const auto* t2 = wf.find_task(TaskId("unit_tests"));
        REQUIRE(t2 != nullptr);
        CHECK(t2->description() == "Run Catch2 test runner");
        CHECK(t2->executable() == utf8_to_path("ctest"));
        CHECK_FALSE(t2->working_directory().has_value());
        CHECK(t2->dependencies() == std::vector<TaskId>{TaskId("compile")});
    }

    SECTION("Minimal valid manifest with optional fields omitted") {
        std::string json = R"({
            "name": "minimal",
            "tasks": [
                {
                    "id": "step1",
                    "executable": "tool.exe"
                }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);

        REQUIRE(result.is_valid());
        const auto& wf = result.workflow();
        CHECK(wf.name() == "minimal");
        REQUIRE(wf.task_count() == 1);

        const auto* t = wf.find_task(TaskId("step1"));
        REQUIRE(t != nullptr);
        CHECK(t->description().empty());
        CHECK(t->arguments().empty());
        CHECK_FALSE(t->working_directory().has_value());
        CHECK(t->dependencies().empty());
    }
}

TEST_CASE("WorkflowManifestParser: Syntax errors and line/column diagnostics", "[manifest]") {
    SECTION("Malformed JSON reports line and column") {
        std::string json = R"({
            "name": "broken",
            "tasks": [
                { "id": "t1", "executable": "a.exe" },
            ]
        })"; // trailing comma is invalid JSON

        auto result = WorkflowManifestParser::parse_string(json);

        REQUIRE_FALSE(result.is_valid());
        CHECK_THROWS_AS(result.workflow(), std::logic_error);

        const auto& diag = result.diagnostics();
        CHECK(diag.has_error_code(ValidationErrorCode::JsonSyntaxError));
        REQUIRE(diag.errors().size() == 1);
        CHECK(diag.errors()[0].code == ValidationErrorCode::JsonSyntaxError);
        // Message contains line information
        CHECK(diag.errors()[0].message.find("line") != std::string::npos);
    }
}

TEST_CASE("WorkflowManifestParser: Schema validation and error accumulation", "[manifest]") {
    SECTION("Root is not a JSON object") {
        auto result = WorkflowManifestParser::parse_string("[\"not\", \"an\", \"object\"]");
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::InvalidSchema));
    }

    SECTION("Missing required root fields and empty tasks array") {
        auto result = WorkflowManifestParser::parse_string("{}");
        REQUIRE_FALSE(result.is_valid());
        const auto& diag = result.diagnostics();
        CHECK(diag.has_error_code(ValidationErrorCode::InvalidSchema));
    }

    SECTION("Empty tasks array produces EmptyWorkflow error") {
        std::string json = R"({
            "name": "empty",
            "tasks": []
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::EmptyWorkflow));
    }

    SECTION("Accumulates multiple independent field type errors in one pass") {
        std::string json = R"({
            "name": 12345,
            "tasks": [
                {
                    "id": "t1",
                    "executable": 999,
                    "arguments": "not-an-array",
                    "working_directory": 42
                },
                {
                    "executable": "run.exe"
                }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        const auto& diag = result.diagnostics();

        // Multiple schema errors should be recorded
        CHECK(diag.errors().size() >= 4);
        CHECK(diag.has_error_code(ValidationErrorCode::InvalidSchema));
    }
}

TEST_CASE("WorkflowManifestParser: Strict schema rules (unknown fields and versions)", "[manifest]") {
    SECTION("Unknown field in manifest root is rejected") {
        std::string json = R"({
            "name": "wf",
            "unknown_root_prop": true,
            "tasks": [
                { "id": "t1", "executable": "run.exe" }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::UnknownField));
    }

    SECTION("Unknown field inside a task object is rejected") {
        std::string json = R"({
            "name": "wf",
            "tasks": [
                { "id": "t1", "executable": "run.exe", "timeout_seconds": 60 }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::UnknownField));
    }

    SECTION("Unsupported version is rejected") {
        std::string json = R"({
            "version": "2.0",
            "name": "wf",
            "tasks": [
                { "id": "t1", "executable": "run.exe" }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::UnsupportedVersion));
    }
}

TEST_CASE("WorkflowManifestParser: Local task constraints and semantic rules", "[manifest]") {
    SECTION("Invalid TaskId syntax is reported") {
        std::string json = R"({
            "name": "wf",
            "tasks": [
                { "id": "bad id with spaces!", "executable": "run.exe" }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::InvalidTaskId));
    }

    SECTION("Empty executable path is reported") {
        std::string json = R"({
            "name": "wf",
            "tasks": [
                { "id": "t1", "executable": "" }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::EmptyExecutable));
    }

    SECTION("Duplicate task IDs in manifest are reported") {
        std::string json = R"({
            "name": "wf",
            "tasks": [
                { "id": "dup", "executable": "a.exe" },
                { "id": "dup", "executable": "b.exe" }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::DuplicateTaskId));
    }

    SECTION("Self-dependency is reported without constructing invalid domain object") {
        std::string json = R"({
            "name": "wf",
            "tasks": [
                { "id": "loop", "executable": "run.exe", "dependencies": ["loop"] }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::SelfDependency));
    }

    SECTION("Duplicate dependencies within task are reported") {
        std::string json = R"({
            "name": "wf",
            "tasks": [
                { "id": "t1", "executable": "a.exe" },
                { "id": "t2", "executable": "b.exe", "dependencies": ["t1", "t1"] }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::DuplicateDependency));
    }

    SECTION("Missing dependency reference is reported by semantic validator") {
        std::string json = R"({
            "name": "wf",
            "tasks": [
                { "id": "t1", "executable": "run.exe", "dependencies": ["nonexistent"] }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::MissingDependency));
    }

    SECTION("Circular dependency is reported with cycle path") {
        std::string json = R"({
            "name": "wf",
            "tasks": [
                { "id": "t1", "executable": "a.exe", "dependencies": ["t2"] },
                { "id": "t2", "executable": "b.exe", "dependencies": ["t1"] }
            ]
        })";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE_FALSE(result.is_valid());
        CHECK(result.diagnostics().has_error_code(ValidationErrorCode::CircularDependency));

        const auto& errors = result.diagnostics().errors();
        REQUIRE(errors.size() == 1);
        CHECK_FALSE(errors[0].cycle_path.empty());
    }
}

TEST_CASE("WorkflowManifestParser: Unicode and UTF-8 path handling", "[manifest]") {
    SECTION("Round-trip conversion for non-ASCII paths") {
        std::vector<std::string> test_paths = {
            "C:/Проекты/build/тест.exe",
            "C:/München/Grüße/äöüß.exe",
            "C:/プロジェクト/日本語/テスト.json",
            "C:/Files/With Spaces/äöü/test.exe"
        };

        for (const auto& expected : test_paths) {
            std::filesystem::path p = utf8_to_path(expected);
            std::string round_trip = path_to_utf8(p);
            CHECK(round_trip == expected);
        }
    }

    SECTION("Explicit error contract: invalid UTF-8 input throws std::invalid_argument") {
        // Invalid continuation byte without lead byte
        CHECK_THROWS_AS(utf8_to_path("\x80"), std::invalid_argument);
        // Invalid lead byte
        CHECK_THROWS_AS(utf8_to_path("\xFF\xFE"), std::invalid_argument);
        // Truncated 2-byte sequence
        CHECK_THROWS_AS(utf8_to_path("\xC2"), std::invalid_argument);
        // Invalid middle byte in 3-byte sequence
        CHECK_THROWS_AS(utf8_to_path("\xE2\x28\xA1"), std::invalid_argument);

        try {
            (void)utf8_to_path("\xFF");
            FAIL("Expected std::invalid_argument was not thrown");
        } catch (const std::invalid_argument& e) {
            CHECK(std::string(e.what()).find("Invalid UTF-8") != std::string::npos);
        }
    }

    SECTION("Unicode characters in executable and working directory paths are preserved") {
        std::string utf8_exec = "C:/Проекты/build/тест.exe";
        std::string utf8_wd = "C:/München/Arbeit/日本語";

        std::string json = "{\n"
            "  \"name\": \"unicode_wf\",\n"
            "  \"tasks\": [\n"
            "    {\n"
            "      \"id\": \"unicode_task\",\n"
            "      \"executable\": \"" + utf8_exec + "\",\n"
            "      \"working_directory\": \"" + utf8_wd + "\"\n"
            "    }\n"
            "  ]\n"
            "}";

        auto result = WorkflowManifestParser::parse_string(json);
        REQUIRE(result.is_valid());

        const auto* task = result.workflow().find_task(TaskId("unicode_task"));
        REQUIRE(task != nullptr);

        CHECK(path_to_utf8(task->executable()) == utf8_exec);
        REQUIRE(task->working_directory().has_value());
        CHECK(path_to_utf8(*task->working_directory()) == utf8_wd);
    }

    SECTION("Real filesystem operations in non-ASCII directory and manifest filename") {
        auto temp_root = std::filesystem::temp_directory_path();
        auto unicode_dir = temp_root / utf8_to_path("taskforge_тест_äöü_日本語");
        std::error_code ec;
        std::filesystem::create_directories(unicode_dir, ec);
        REQUIRE_FALSE(ec);

        auto manifest_file = unicode_dir / utf8_to_path("манифест_テスト.json");
        std::string json = R"({
            "name": "unicode_file_wf",
            "tasks": [
                { "id": "t1", "executable": "cmake.exe" }
            ]
        })";

        {
            std::ofstream out(manifest_file, std::ios::binary);
            REQUIRE(out.is_open());
            out << json;
        }

        // Test real parse_file on the non-ASCII path
        auto result_opt = WorkflowManifestParser::parse_file(manifest_file);
        REQUIRE(result_opt.has_value());
        REQUIRE(result_opt->is_valid());
        CHECK(result_opt->workflow().name() == "unicode_file_wf");

        // Clean up
        std::filesystem::remove(manifest_file, ec);
        std::filesystem::remove(unicode_dir, ec);
    }
}

TEST_CASE("ManifestParseResult API contract", "[manifest]") {
    SECTION("failure() rejects valid diagnostics") {
        ValidationResult empty_diag;
        REQUIRE(empty_diag.is_valid());
        CHECK_THROWS_AS(ManifestParseResult::failure(std::move(empty_diag)), std::invalid_argument);
    }
}
