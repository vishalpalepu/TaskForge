#include "taskforge/core/task_result.hpp"
#include "taskforge/core/task_state.hpp"
#include "taskforge/scheduler/process_executor.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sstream>

using namespace taskforge::core;

TEST_CASE("TaskState enum conversions and streaming", "[core][task_state]") {
    SECTION("to_string returns expected strings for all TaskState values") {
        CHECK(to_string(TaskState::Pending) == "Pending");
        CHECK(to_string(TaskState::Ready) == "Ready");
        CHECK(to_string(TaskState::Running) == "Running");
        CHECK(to_string(TaskState::Succeeded) == "Succeeded");
        CHECK(to_string(TaskState::Failed) == "Failed");
        CHECK(to_string(TaskState::Blocked) == "Blocked");
    }

    SECTION("operator<< writes to std::ostream") {
        std::ostringstream oss;
        oss << TaskState::Succeeded << " " << TaskState::Blocked;
        CHECK(oss.str() == "Succeeded Blocked");
    }
}

TEST_CASE("FailureCategory enum conversions and streaming", "[core][failure_category]") {
    SECTION("to_string returns expected strings for all FailureCategory values") {
        CHECK(to_string(FailureCategory::None) == "None");
        CHECK(to_string(FailureCategory::LaunchError) == "LaunchError");
        CHECK(to_string(FailureCategory::NonZeroExit) == "NonZeroExit");
        CHECK(to_string(FailureCategory::SystemFault) == "SystemFault");
        CHECK(to_string(FailureCategory::Timeout) == "Timeout");
    }

    SECTION("operator<< writes to std::ostream") {
        std::ostringstream oss;
        oss << FailureCategory::LaunchError << " " << FailureCategory::NonZeroExit;
        CHECK(oss.str() == "LaunchError NonZeroExit");
    }
}

TEST_CASE("TaskResult factory invariants and queries", "[core][task_result]") {
    auto id = TaskId("build_task");
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::milliseconds{150};

    SECTION("create_succeeded establishes successful terminal invariants") {
        auto res = TaskResult::create_succeeded(
            id,
            std::make_optional(0),
            "Build output",
            "",
            now,
            now + duration,
            duration);

        CHECK(res.task_id() == id);
        CHECK(res.final_state() == TaskState::Succeeded);
        CHECK(res.failure_category() == FailureCategory::None);
        CHECK(res.is_success());
        CHECK_FALSE(res.is_failed());
        CHECK_FALSE(res.is_blocked());
        REQUIRE(res.exit_code().has_value());
        CHECK(res.exit_code().value() == 0);
        CHECK_FALSE(res.os_error_code().has_value());
        CHECK(res.standard_output() == "Build output");
        CHECK(res.standard_error().empty());
        CHECK_FALSE(res.error_message().has_value());
        CHECK(res.duration() == duration);
    }

    SECTION("create_failed for NonZeroExit") {
        auto res = TaskResult::create_failed(
            id,
            FailureCategory::NonZeroExit,
            std::make_optional(2),
            std::nullopt,
            "",
            "Compilation failed",
            "Exit code 2",
            now,
            now + duration,
            duration);

        CHECK(res.task_id() == id);
        CHECK(res.final_state() == TaskState::Failed);
        CHECK(res.failure_category() == FailureCategory::NonZeroExit);
        CHECK_FALSE(res.is_success());
        CHECK(res.is_failed());
        CHECK(res.is_execution_failure());
        CHECK_FALSE(res.is_launch_failure());
        REQUIRE(res.exit_code().has_value());
        CHECK(res.exit_code().value() == 2);
        CHECK_FALSE(res.os_error_code().has_value());
        CHECK(res.standard_error() == "Compilation failed");
        REQUIRE(res.error_message().has_value());
        CHECK(res.error_message().value() == "Exit code 2");
    }

    SECTION("create_failed for LaunchError") {
        auto res = TaskResult::create_failed(
            id,
            FailureCategory::LaunchError,
            std::nullopt,
            std::make_optional(2ul), // ERROR_FILE_NOT_FOUND
            "",
            "",
            "Binary not found on PATH",
            now,
            now,
            std::chrono::milliseconds{0});

        CHECK(res.final_state() == TaskState::Failed);
        CHECK(res.failure_category() == FailureCategory::LaunchError);
        CHECK(res.is_failed());
        CHECK(res.is_launch_failure());
        CHECK_FALSE(res.is_execution_failure());
        CHECK_FALSE(res.exit_code().has_value());
        REQUIRE(res.os_error_code().has_value());
        CHECK(res.os_error_code().value() == 2ul);
        REQUIRE(res.error_message().has_value());
        CHECK(res.error_message().value() == "Binary not found on PATH");
    }

    SECTION("create_blocked establishes blocked terminal invariants") {
        auto res = TaskResult::create_blocked(
            id,
            "Prerequisite upstream_task failed");

        CHECK(res.task_id() == id);
        CHECK(res.final_state() == TaskState::Blocked);
        CHECK(res.failure_category() == FailureCategory::None);
        CHECK_FALSE(res.is_success());
        CHECK_FALSE(res.is_failed());
        CHECK(res.is_blocked());
        CHECK_FALSE(res.exit_code().has_value());
        CHECK_FALSE(res.os_error_code().has_value());
        CHECK(res.duration() == std::chrono::milliseconds{0});
        REQUIRE(res.error_message().has_value());
        CHECK(res.error_message().value() == "Prerequisite upstream_task failed");
    }

    SECTION("rejects negative durations with std::invalid_argument") {
        auto negative_dur = std::chrono::milliseconds{-10};
        CHECK_THROWS_AS(
            TaskResult::create_succeeded(id, 0, "", "", now, now, negative_dur),
            std::invalid_argument);
        CHECK_THROWS_AS(
            TaskResult::create_failed(id, FailureCategory::NonZeroExit, 1, std::nullopt, "", "", "", now, now, negative_dur),
            std::invalid_argument);
    }
}

TEST_CASE("ExecutionResponse factory invariants and negative duration rejection", "[scheduler][process_executor]") {
    using taskforge::scheduler::ExecutionResponse;
    auto neg = std::chrono::milliseconds{-1};

    SECTION("rejects negative duration across all factories") {
        CHECK_THROWS_AS(ExecutionResponse::completed(0, "", "", neg), std::invalid_argument);
        CHECK_THROWS_AS(ExecutionResponse::launch_failure(2ul, "err", neg), std::invalid_argument);
        CHECK_THROWS_AS(ExecutionResponse::system_fault("err", neg), std::invalid_argument);
    }

    SECTION("completed factory invariants") {
        auto resp = ExecutionResponse::completed(0, "out", "err", std::chrono::milliseconds{10});
        CHECK(resp.is_successful());
        CHECK_FALSE(resp.is_launch_failure());
        CHECK_FALSE(resp.is_system_fault());
        REQUIRE(resp.exit_code().has_value());
        CHECK(resp.exit_code().value() == 0);
        CHECK_FALSE(resp.os_error_code().has_value());
        CHECK(resp.standard_output() == "out");
        CHECK(resp.standard_error() == "err");
        CHECK(resp.duration() == std::chrono::milliseconds{10});
    }

    SECTION("launch_failure invariants have no fabricated exit code") {
        auto resp = ExecutionResponse::launch_failure(5ul, "access denied");
        CHECK_FALSE(resp.is_successful());
        CHECK(resp.is_launch_failure());
        CHECK_FALSE(resp.is_system_fault());
        CHECK_FALSE(resp.exit_code().has_value()); // No fabricated exit code!
        REQUIRE(resp.os_error_code().has_value());
        CHECK(resp.os_error_code().value() == 5ul);
        CHECK(resp.error_message() == "access denied");
    }

    SECTION("system_fault invariants") {
        auto resp = ExecutionResponse::system_fault("fatal pipe error");
        CHECK_FALSE(resp.is_successful());
        CHECK_FALSE(resp.is_launch_failure());
        CHECK(resp.is_system_fault());
        CHECK_FALSE(resp.exit_code().has_value());
        CHECK_FALSE(resp.os_error_code().has_value());
        CHECK(resp.error_message() == "fatal pipe error");
    }
}
