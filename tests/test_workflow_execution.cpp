#include "taskforge/core/workflow_execution.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace taskforge::core;

TEST_CASE("WorkflowExecution aggregate behavior", "[core][workflow_execution]") {
    auto t1 = TaskId("task_1");
    auto t2 = TaskId("task_2");
    auto t3 = TaskId("task_3");

    auto now = std::chrono::system_clock::now();
    auto dur = std::chrono::milliseconds{100};

    SECTION("Workflow with all successful tasks") {
        std::unordered_map<TaskId, TaskResult> results;
        results.emplace(t1, TaskResult::create_succeeded(t1, 0, "ok", "", now, now + dur, dur));
        results.emplace(t2, TaskResult::create_succeeded(t2, 0, "ok", "", now + dur, now + dur * 2, dur));

        std::vector<TaskId> order = {t1, t2};

        WorkflowExecution execution("all_success", std::move(results), std::move(order), now, now + dur * 2);

        CHECK(execution.workflow_name() == "all_success");
        CHECK(execution.task_count() == 2);
        CHECK(execution.execution_order().size() == 2);
        CHECK(execution.execution_order()[0] == t1);
        CHECK(execution.execution_order()[1] == t2);

        CHECK(execution.task_state(t1) == TaskState::Succeeded);
        CHECK(execution.task_state(t2) == TaskState::Succeeded);
        REQUIRE(execution.task_result(t1) != nullptr);
        CHECK(execution.task_result(t1)->is_success());

        CHECK(execution.is_successful());
        CHECK_FALSE(execution.has_failures());
        CHECK(execution.count_with_state(TaskState::Succeeded) == 2);
        CHECK(execution.count_with_state(TaskState::Failed) == 0);
        CHECK(execution.count_with_state(TaskState::Blocked) == 0);
        CHECK(execution.total_duration() == dur * 2);

        // Unknown task throws
        auto unknown = TaskId("unknown_task");
        CHECK_THROWS_AS(execution.task_state(unknown), std::out_of_range);
        CHECK(execution.task_result(unknown) == nullptr);
    }

    SECTION("Workflow with failed and blocked tasks") {
        std::unordered_map<TaskId, TaskResult> results;
        results.emplace(t1, TaskResult::create_succeeded(t1, 0, "ok", "", now, now + dur, dur));
        results.emplace(t2, TaskResult::create_failed(
            t2, FailureCategory::NonZeroExit, 1, std::nullopt, "", "err", "fail", now + dur, now + dur * 2, dur));
        results.emplace(t3, TaskResult::create_blocked(t3, "prereq failed"));

        std::vector<TaskId> order = {t1, t2}; // t3 was never dispatched

        WorkflowExecution execution("mixed_workflow", std::move(results), std::move(order), now, now + dur * 2);

        CHECK_FALSE(execution.is_successful());
        CHECK(execution.has_failures());
        CHECK(execution.count_with_state(TaskState::Succeeded) == 1);
        CHECK(execution.count_with_state(TaskState::Failed) == 1);
        CHECK(execution.count_with_state(TaskState::Blocked) == 1);
        CHECK(execution.task_state(t3) == TaskState::Blocked);
        REQUIRE(execution.task_result(t3) != nullptr);
        CHECK(execution.task_result(t3)->is_blocked());
    }

    SECTION("Value semantics (copy and move)") {
        std::unordered_map<TaskId, TaskResult> results;
        results.emplace(t1, TaskResult::create_succeeded(t1, 0, "ok", "", now, now + dur, dur));

        WorkflowExecution exec1("test_wf", std::move(results), {t1}, now, now + dur);
        WorkflowExecution exec2 = exec1; // Copy

        CHECK(exec2.workflow_name() == "test_wf");
        CHECK(exec2.task_count() == 1);
        CHECK(exec2.task_state(t1) == TaskState::Succeeded);

        WorkflowExecution exec3 = std::move(exec1); // Move
        CHECK(exec3.workflow_name() == "test_wf");
        CHECK(exec3.task_count() == 1);
    }

    SECTION("total_duration clamps to 0 when end_time is earlier than start_time") {
        std::unordered_map<TaskId, TaskResult> results;
        results.emplace(t1, TaskResult::create_succeeded(t1, 0, "ok", "", now, now, dur));

        // end_time is before start_time
        WorkflowExecution exec("skewed_clock", std::move(results), {t1}, now + dur, now);
        CHECK(exec.total_duration() == std::chrono::milliseconds{0});
    }
}
