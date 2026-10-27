#include "fakes/fake_process_executor.hpp"
#include "taskforge/scheduler/workflow_scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace taskforge::core;
using namespace taskforge::scheduler;
using namespace taskforge::testing;

TEST_CASE("WorkflowScheduler: Sequential scheduling engine scenarios", "[scheduler]") {
    FakeProcessExecutor executor;
    WorkflowScheduler scheduler(executor);

    SECTION("Scenario 1: Single task workflow executes cleanly to Succeeded") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task1"), "echo task", "echo.exe", {"hello"}));

        auto workflow = WorkflowDefinition::create("single_task_wf", std::move(tasks));
        executor.set_default_response(ExecutionResponse::completed(0, "hello\n", "", std::chrono::milliseconds{10}));

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        REQUIRE(result.is_successful());
        const auto& exec = result.execution();

        CHECK(exec.task_count() == 1);
        CHECK(exec.task_state(TaskId("task1")) == TaskState::Succeeded);
        CHECK(exec.execution_order() == std::vector<TaskId>{TaskId("task1")});
        REQUIRE(exec.task_result(TaskId("task1")) != nullptr);
        CHECK(exec.task_result(TaskId("task1"))->exit_code() == 0);
        CHECK(exec.task_result(TaskId("task1"))->standard_output() == "hello\n");
        CHECK(executor.execution_count() == 1);
    }

    SECTION("Scenario 2: Linear pipeline (A -> B -> C) executes in strict dependency order") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task_A"), "step A", "stepA.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("task_B"), "step B", "stepB.exe", {}, std::nullopt, {TaskId("task_A")}));
        tasks.push_back(TaskDefinition::create(TaskId("task_C"), "step C", "stepC.exe", {}, std::nullopt, {TaskId("task_B")}));

        auto workflow = WorkflowDefinition::create("linear_pipeline", std::move(tasks));
        executor.set_default_response(ExecutionResponse::completed(0, "", "", std::chrono::milliseconds{5}));

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        REQUIRE(result.is_successful());
        const auto& exec = result.execution();

        std::vector<TaskId> expected_order = {
            TaskId("task_A"),
            TaskId("task_B"),
            TaskId("task_C")
        };
        CHECK(exec.execution_order() == expected_order);
        CHECK(exec.count_with_state(TaskState::Succeeded) == 3);
        CHECK(executor.execution_count() == 3);
    }

    SECTION("Scenario 3: Diamond dependency (A -> B, A -> C, B -> D, C -> D)") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task_A"), "step A", "A.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("task_B"), "step B", "B.exe", {}, std::nullopt, {TaskId("task_A")}));
        tasks.push_back(TaskDefinition::create(TaskId("task_C"), "step C", "C.exe", {}, std::nullopt, {TaskId("task_A")}));
        tasks.push_back(TaskDefinition::create(TaskId("task_D"), "step D", "D.exe", {}, std::nullopt, {TaskId("task_B"), TaskId("task_C")}));

        auto workflow = WorkflowDefinition::create("diamond_wf", std::move(tasks));
        executor.set_default_response(ExecutionResponse::completed(0, "", "", std::chrono::milliseconds{5}));

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        REQUIRE(result.is_successful());
        const auto& exec = result.execution();

        // A must be first, D must be last; B and C in between in declaration order (B then C)
        std::vector<TaskId> expected_order = {
            TaskId("task_A"),
            TaskId("task_B"),
            TaskId("task_C"),
            TaskId("task_D")
        };
        CHECK(exec.execution_order() == expected_order);
        CHECK(exec.count_with_state(TaskState::Succeeded) == 4);
        CHECK(executor.execution_count() == 4);
    }

    SECTION("Scenario 4: Deterministic declaration-order tie-breaking") {
        // Independent tasks with alphabetical order different from declaration order
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("zebra"), "desc zebra", "run.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("alpha"), "desc alpha", "run.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("beta"), "desc beta", "run.exe", {}));

        auto workflow = WorkflowDefinition::create("tie_break_wf", std::move(tasks));
        executor.set_default_response(ExecutionResponse::completed(0, "", "", std::chrono::milliseconds{5}));

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        const auto& exec = result.execution();

        // Must run in declaration order: zebra, alpha, beta
        std::vector<TaskId> expected_order = {
            TaskId("zebra"),
            TaskId("alpha"),
            TaskId("beta")
        };
        CHECK(exec.execution_order() == expected_order);
    }

    SECTION("Scenario 5: Upstream task failure blocks downstream tasks") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task_A"), "step A", "fail.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("task_B"), "step B", "stepB.exe", {}, std::nullopt, {TaskId("task_A")}));
        tasks.push_back(TaskDefinition::create(TaskId("task_C"), "step C", "stepC.exe", {}, std::nullopt, {TaskId("task_B")}));

        auto workflow = WorkflowDefinition::create("upstream_failure_wf", std::move(tasks));
        // task_A exits with non-zero code 1
        executor.set_response_for("fail.exe", ExecutionResponse::completed(1, "", "syntax error", std::chrono::milliseconds{5}));

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        CHECK_FALSE(result.is_successful());
        const auto& exec = result.execution();

        CHECK(exec.task_state(TaskId("task_A")) == TaskState::Failed);
        CHECK(exec.task_state(TaskId("task_B")) == TaskState::Blocked);
        CHECK(exec.task_state(TaskId("task_C")) == TaskState::Blocked);

        // Only task_A was ever dispatched to executor
        CHECK(executor.execution_count() == 1);
        CHECK(exec.execution_order() == std::vector<TaskId>{TaskId("task_A")});

        // Blocked tasks record the reason
        const auto* res_b = exec.task_result(TaskId("task_B"));
        REQUIRE(res_b != nullptr);
        CHECK(res_b->is_blocked());
        REQUIRE(res_b->error_message().has_value());
        CHECK(res_b->error_message()->find("task_A") != std::string::npos);
    }

    SECTION("Scenario 6: Independent branch continues when another branch fails") {
        // Branch 1: A -> B (A fails -> B blocked)
        // Branch 2: C -> D (independent, C succeeds, D succeeds)
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task_A"), "step A", "failA.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("task_B"), "step B", "stepB.exe", {}, std::nullopt, {TaskId("task_A")}));
        tasks.push_back(TaskDefinition::create(TaskId("task_C"), "step C", "stepC.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("task_D"), "step D", "stepD.exe", {}, std::nullopt, {TaskId("task_C")}));

        auto workflow = WorkflowDefinition::create("multi_branch_wf", std::move(tasks));
        executor.set_default_response(ExecutionResponse::completed(0, "", "", std::chrono::milliseconds{5}));
        executor.set_response_for("failA.exe", ExecutionResponse::completed(42, "", "error", std::chrono::milliseconds{5}));

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        CHECK_FALSE(result.is_successful());
        const auto& exec = result.execution();

        CHECK(exec.task_state(TaskId("task_A")) == TaskState::Failed);
        CHECK(exec.task_state(TaskId("task_B")) == TaskState::Blocked);
        CHECK(exec.task_state(TaskId("task_C")) == TaskState::Succeeded);
        CHECK(exec.task_state(TaskId("task_D")) == TaskState::Succeeded);

        // A, C, D dispatched (total 3). B was never dispatched.
        CHECK(executor.execution_count() == 3);
        CHECK(exec.count_with_state(TaskState::Succeeded) == 2);
        CHECK(exec.count_with_state(TaskState::Failed) == 1);
        CHECK(exec.count_with_state(TaskState::Blocked) == 1);
    }

    SECTION("Scenario 7: Launch failure distinguishes from non-zero exit code") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("missing_bin"), "desc", "nonexistent.exe", {}));

        auto workflow = WorkflowDefinition::create("launch_fail_wf", std::move(tasks));
        // Simulate OS launch failure (e.g. ERROR_FILE_NOT_FOUND = 2)
        executor.set_response_for(
            "nonexistent.exe",
            ExecutionResponse::launch_failure(2ul, "CreateProcess failed: file not found"));

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        CHECK_FALSE(result.is_successful());
        const auto& exec = result.execution();

        const auto* res = exec.task_result(TaskId("missing_bin"));
        REQUIRE(res != nullptr);
        CHECK(res->final_state() == TaskState::Failed);
        CHECK(res->failure_category() == FailureCategory::LaunchError);
        CHECK(res->is_launch_failure());
        CHECK_FALSE(res->is_execution_failure());
        CHECK_FALSE(res->exit_code().has_value());
        REQUIRE(res->os_error_code().has_value());
        CHECK(res->os_error_code().value() == 2ul);
        REQUIRE(res->error_message().has_value());
        CHECK(res->error_message()->find("CreateProcess failed") != std::string::npos);
    }

    SECTION("Scenario 8: Invalid workflow (cyclic) rejected before execution with zero dispatches") {
        // Indirect cycle: A prereq B, B prereq A
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task_A"), "step A", "A.exe", {}, std::nullopt, {TaskId("task_B")}));
        tasks.push_back(TaskDefinition::create(TaskId("task_B"), "step B", "B.exe", {}, std::nullopt, {TaskId("task_A")}));

        auto workflow = WorkflowDefinition::create("cyclic_wf", std::move(tasks));

        auto result = scheduler.run(workflow);

        // Validation fails; no execution produced
        CHECK_FALSE(result.is_valid());
        CHECK_FALSE(result.is_successful());
        CHECK(result.execution_ptr() == nullptr);
        CHECK_THROWS_AS(result.execution(), std::logic_error);

        const auto& val = result.validation_result();
        CHECK(val.has_error_code(ValidationErrorCode::CircularDependency));

        // Zero executor dispatches
        CHECK(executor.execution_count() == 0);
    }

    SECTION("Scenario 9: Workflow with missing dependency rejected before execution") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task_A"), "step A", "A.exe", {}, std::nullopt, {TaskId("non_existent")}));

        auto workflow = WorkflowDefinition::create("missing_dep_wf", std::move(tasks));

        auto result = scheduler.run(workflow);

        CHECK_FALSE(result.is_valid());
        CHECK(executor.execution_count() == 0);
        const auto& val = result.validation_result();
        CHECK(val.has_error_code(ValidationErrorCode::MissingDependency));
    }

    SECTION("Scenario 10: At-most-once execution in multi-parent diamond graph") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("root"), "step root", "root.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("left"), "step left", "left.exe", {}, std::nullopt, {TaskId("root")}));
        tasks.push_back(TaskDefinition::create(TaskId("right"), "step right", "right.exe", {}, std::nullopt, {TaskId("root")}));
        tasks.push_back(TaskDefinition::create(TaskId("join"), "step join", "join.exe", {}, std::nullopt, {TaskId("left"), TaskId("right")}));

        auto workflow = WorkflowDefinition::create("at_most_once_wf", std::move(tasks));
        executor.set_default_response(ExecutionResponse::completed(0, "", "", std::chrono::milliseconds{5}));

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        const auto& exec = result.execution();

        CHECK(exec.execution_order().size() == 4);
        CHECK(executor.execution_count() == 4);

        // Verify each task ID appears exactly once in recorded requests
        std::map<std::string, int> dispatch_counts;
        for (const auto& req : executor.recorded_requests()) {
            dispatch_counts[req.executable.string()]++;
        }
        CHECK(dispatch_counts["root.exe"] == 1);
        CHECK(dispatch_counts["left.exe"] == 1);
        CHECK(dispatch_counts["right.exe"] == 1);
        CHECK(dispatch_counts["join.exe"] == 1);
    }

    SECTION("Scenario 11: Unexpected executor exception caught gracefully as SystemFault") {
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("faulty_task"), "step faulty", "crash.exe", {}));

        auto workflow = WorkflowDefinition::create("system_fault_wf", std::move(tasks));
        executor.set_throw_on_execute(true, "Simulated kernel pipe error");

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        CHECK_FALSE(result.is_successful());
        const auto& exec = result.execution();

        const auto* res = exec.task_result(TaskId("faulty_task"));
        REQUIRE(res != nullptr);
        CHECK(res->final_state() == TaskState::Failed);
        CHECK(res->failure_category() == FailureCategory::SystemFault);
        CHECK_FALSE(res->is_success());
        CHECK_FALSE(res->is_launch_failure());
        CHECK_FALSE(res->is_execution_failure());
        REQUIRE(res->error_message().has_value());
        CHECK(res->error_message()->find("Simulated kernel pipe error") != std::string::npos);
    }

    SECTION("Scenario 12: Pruned BFS does not re-expand multi-path blocked nodes") {
        // Multi-path failure topology:
        // A fails
        // B depends on A
        // C depends on A
        // D depends on B and C
        // E depends on D
        // B and C both get blocked. When D is blocked via B, it shouldn't cause redundant traversal
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("task_A"), "step A", "fail.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("task_B"), "step B", "b.exe", {}, std::nullopt, {TaskId("task_A")}));
        tasks.push_back(TaskDefinition::create(TaskId("task_C"), "step C", "c.exe", {}, std::nullopt, {TaskId("task_A")}));
        tasks.push_back(TaskDefinition::create(TaskId("task_D"), "step D", "d.exe", {}, std::nullopt, {TaskId("task_B"), TaskId("task_C")}));
        tasks.push_back(TaskDefinition::create(TaskId("task_E"), "step E", "e.exe", {}, std::nullopt, {TaskId("task_D")}));

        auto workflow = WorkflowDefinition::create("multipath_block_wf", std::move(tasks));
        executor.set_response_for("fail.exe", ExecutionResponse::completed(1, "", "failed", std::chrono::milliseconds{5}));

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        const auto& exec = result.execution();

        CHECK(exec.task_state(TaskId("task_A")) == TaskState::Failed);
        CHECK(exec.task_state(TaskId("task_B")) == TaskState::Blocked);
        CHECK(exec.task_state(TaskId("task_C")) == TaskState::Blocked);
        CHECK(exec.task_state(TaskId("task_D")) == TaskState::Blocked);
        CHECK(exec.task_state(TaskId("task_E")) == TaskState::Blocked);

        CHECK(exec.count_with_state(TaskState::Blocked) == 4);
        CHECK(exec.count_with_state(TaskState::Failed) == 1);
        CHECK(executor.execution_count() == 1);
    }

    SECTION("Scenario 13: WorkflowRunResult API contract invariants") {
        // Test valid workflow
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("t1"), "step t1", "ok.exe", {}));
        auto valid_wf = WorkflowDefinition::create("valid_wf", std::move(tasks));
        executor.set_default_response(ExecutionResponse::completed(0, "", "", std::chrono::milliseconds{5}));

        auto valid_res = scheduler.run(valid_wf);
        CHECK(valid_res.is_valid());
        CHECK_NOTHROW(valid_res.execution());
        CHECK_THROWS_AS(valid_res.validation_result(), std::logic_error);

        // Test invalid workflow
        tasks.clear();
        tasks.push_back(TaskDefinition::create(TaskId("t2"), "step t2", "ok.exe", {}, std::nullopt, {TaskId("missing")}));
        auto invalid_wf = WorkflowDefinition::create("invalid_wf", std::move(tasks));

        auto invalid_res = scheduler.run(invalid_wf);
        CHECK_FALSE(invalid_res.is_valid());
        CHECK_NOTHROW(invalid_res.validation_result());
        CHECK_THROWS_AS(invalid_res.execution(), std::logic_error);

        // Factory contract: validation_failure rejects valid ValidationResult
        ValidationResult ok_val;
        REQUIRE(ok_val.is_valid());
        CHECK_THROWS_AS(WorkflowRunResult::validation_failure(std::move(ok_val)), std::invalid_argument);
    }

    SECTION("Scenario 14: Empty workflow rejected before execution with zero dispatches") {
        auto empty_wf = WorkflowDefinition::create("empty_wf", {});

        auto result = scheduler.run(empty_wf);

        CHECK_FALSE(result.is_valid());
        CHECK_FALSE(result.is_successful());
        CHECK(executor.execution_count() == 0);
        const auto& val = result.validation_result();
        CHECK(val.has_error_code(ValidationErrorCode::EmptyWorkflow));
    }

    SECTION("Scenario 15: Dual-root overlapping failure paths to shared join task") {
        // A1 (fails) -> B1 -> Join -> End
        // A2 (fails) -> B2 -> Join -> End
        std::vector<TaskDefinition> tasks;
        tasks.push_back(TaskDefinition::create(TaskId("A1"), "step A1", "fail1.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("A2"), "step A2", "fail2.exe", {}));
        tasks.push_back(TaskDefinition::create(TaskId("B1"), "step B1", "b1.exe", {}, std::nullopt, {TaskId("A1")}));
        tasks.push_back(TaskDefinition::create(TaskId("B2"), "step B2", "b2.exe", {}, std::nullopt, {TaskId("A2")}));
        tasks.push_back(TaskDefinition::create(TaskId("Join"), "step Join", "join.exe", {}, std::nullopt, {TaskId("B1"), TaskId("B2")}));
        tasks.push_back(TaskDefinition::create(TaskId("End"), "step End", "end.exe", {}, std::nullopt, {TaskId("Join")}));

        auto workflow = WorkflowDefinition::create("dual_failure_wf", std::move(tasks));
        executor.set_response_for("fail1.exe", ExecutionResponse::completed(1, "", "fail1", std::chrono::milliseconds{5}));
        executor.set_response_for("fail2.exe", ExecutionResponse::completed(2, "", "fail2", std::chrono::milliseconds{5}));

        auto result = scheduler.run(workflow);

        REQUIRE(result.is_valid());
        CHECK_FALSE(result.is_successful());
        const auto& exec = result.execution();

        CHECK(exec.task_state(TaskId("A1")) == TaskState::Failed);
        CHECK(exec.task_state(TaskId("A2")) == TaskState::Failed);
        CHECK(exec.task_state(TaskId("B1")) == TaskState::Blocked);
        CHECK(exec.task_state(TaskId("B2")) == TaskState::Blocked);
        CHECK(exec.task_state(TaskId("Join")) == TaskState::Blocked);
        CHECK(exec.task_state(TaskId("End")) == TaskState::Blocked);

        CHECK(exec.count_with_state(TaskState::Failed) == 2);
        CHECK(exec.count_with_state(TaskState::Blocked) == 4);
        CHECK(exec.count_with_state(TaskState::Succeeded) == 0);
        // Only A1 and A2 were executed
        CHECK(executor.execution_count() == 2);
    }
}
