#include "taskforge/scheduler/workflow_scheduler.hpp"
#include "taskforge/core/workflow_validator.hpp"

#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace taskforge::scheduler {

// ---------------------------------------------------------------------------
// WorkflowRunResult implementation
// ---------------------------------------------------------------------------

WorkflowRunResult WorkflowRunResult::validation_failure(core::ValidationResult result) {
    if (result.is_valid()) {
        throw std::invalid_argument("Cannot create a validation_failure WorkflowRunResult with a valid ValidationResult.");
    }
    return WorkflowRunResult(std::move(result));
}

WorkflowRunResult WorkflowRunResult::executed(core::WorkflowExecution execution) {
    return WorkflowRunResult(std::move(execution));
}

WorkflowRunResult::WorkflowRunResult(core::ValidationResult result)
    : result_(std::move(result)) {}

WorkflowRunResult::WorkflowRunResult(core::WorkflowExecution execution)
    : result_(std::move(execution)) {}

bool WorkflowRunResult::is_valid() const noexcept {
    return std::holds_alternative<core::WorkflowExecution>(result_);
}

bool WorkflowRunResult::is_successful() const noexcept {
    if (const auto* exec = std::get_if<core::WorkflowExecution>(&result_)) {
        return exec->is_successful();
    }
    return false;
}

const core::ValidationResult& WorkflowRunResult::validation_result() const {
    if (const auto* val = std::get_if<core::ValidationResult>(&result_)) {
        return *val;
    }
    throw std::logic_error("WorkflowRunResult::validation_result() called on a valid execution result.");
}

const core::WorkflowExecution& WorkflowRunResult::execution() const {
    if (const auto* exec = std::get_if<core::WorkflowExecution>(&result_)) {
        return *exec;
    }
    throw std::logic_error("WorkflowRunResult::execution() called on a validation failure result.");
}

const core::WorkflowExecution* WorkflowRunResult::execution_ptr() const noexcept {
    return std::get_if<core::WorkflowExecution>(&result_);
}

// ---------------------------------------------------------------------------
// WorkflowScheduler implementation
// ---------------------------------------------------------------------------

WorkflowScheduler::WorkflowScheduler(IProcessExecutor& executor) noexcept
    : executor_(executor) {}

WorkflowRunResult WorkflowScheduler::run(const core::WorkflowDefinition& workflow) {
    // 1. Pre-execution semantic validation
    auto val_result = core::WorkflowValidator::validate(workflow);
    if (!val_result.is_valid()) {
        return WorkflowRunResult::validation_failure(std::move(val_result));
    }

    // 2. Build dependency adjacency and in-degrees
    std::unordered_map<core::TaskId, size_t> in_degrees;
    std::unordered_map<core::TaskId, std::vector<core::TaskId>> dependents;
    std::unordered_map<core::TaskId, core::TaskState> task_states;
    std::unordered_map<core::TaskId, core::TaskResult> results;
    std::vector<core::TaskId> execution_order;

    for (const auto& task : workflow.tasks()) {
        in_degrees[task.id()] = task.dependencies().size();
        for (const auto& prereq : task.dependencies()) {
            dependents[prereq].push_back(task.id());
        }
    }

    // 3. Deterministic ready-queue ordering by declaration index
    struct DeterministicTaskOrder {
        const core::WorkflowDefinition* wf;
        bool operator()(const core::TaskId& a, const core::TaskId& b) const noexcept {
            const auto* ta = wf->find_task(a);
            const auto* tb = wf->find_task(b);
            if (ta && tb) {
                return ta->declaration_index() < tb->declaration_index();
            }
            return a < b;
        }
    };

    DeterministicTaskOrder order_cmp{&workflow};
    std::set<core::TaskId, DeterministicTaskOrder> ready_queue(order_cmp);

    for (const auto& task : workflow.tasks()) {
        if (in_degrees[task.id()] == 0) {
            task_states[task.id()] = core::TaskState::Ready;
            ready_queue.insert(task.id());
        } else {
            task_states[task.id()] = core::TaskState::Pending;
        }
    }

    // 4. Sequential execution loop
    auto start_time = std::chrono::system_clock::now();

    while (!ready_queue.empty()) {
        auto it = ready_queue.begin();
        core::TaskId current_id = *it;
        ready_queue.erase(it);

        if (task_states[current_id] != core::TaskState::Ready) {
            throw std::logic_error(
                "Internal scheduling invariant violated: task in ready_queue is not Ready: " +
                current_id.value());
        }

        task_states[current_id] = core::TaskState::Running;
        const auto* task_def = workflow.find_task(current_id);
        if (!task_def) {
            throw std::logic_error("Task definition not found during execution: " + current_id.value());
        }

        ExecutionRequest req;
        req.executable = task_def->executable();
        req.arguments = task_def->arguments();
        req.working_directory = task_def->working_directory();

        auto task_start = std::chrono::system_clock::now();
        ExecutionResponse resp = ExecutionResponse::system_fault("Uninitialized response");
        try {
            resp = executor_.execute(req);
        } catch (const std::exception& ex) {
            resp = ExecutionResponse::system_fault(ex.what());
        } catch (...) {
            resp = ExecutionResponse::system_fault("Unknown executor exception");
        }
        auto task_end = std::chrono::system_clock::now();
        auto task_duration = std::chrono::duration_cast<std::chrono::milliseconds>(task_end - task_start);
        if (task_duration < std::chrono::milliseconds{0}) {
            task_duration = std::chrono::milliseconds{0};
        }
        if (resp.duration().count() > 0) {
            task_duration = resp.duration();
        }

        if (resp.is_successful()) {
            task_states[current_id] = core::TaskState::Succeeded;
            results.emplace(
                current_id,
                core::TaskResult::create_succeeded(
                    current_id,
                    resp.exit_code(),
                    resp.standard_output(),
                    resp.standard_error(),
                    task_start,
                    task_end,
                    task_duration));
            execution_order.push_back(current_id);

            auto dep_it = dependents.find(current_id);
            if (dep_it != dependents.end()) {
                for (const auto& dep_id : dep_it->second) {
                    if (task_states[dep_id] == core::TaskState::Pending) {
                        auto& deg = in_degrees[dep_id];
                        if (deg > 0) {
                            --deg;
                            if (deg == 0) {
                                task_states[dep_id] = core::TaskState::Ready;
                                ready_queue.insert(dep_id);
                            }
                        }
                    }
                }
            }
        } else {
            // Task failed (LaunchError, SystemFault, or NonZeroExit)
            core::FailureCategory cat = core::FailureCategory::NonZeroExit;
            if (resp.is_launch_failure()) {
                cat = core::FailureCategory::LaunchError;
            } else if (resp.is_system_fault()) {
                cat = core::FailureCategory::SystemFault;
            }

            task_states[current_id] = core::TaskState::Failed;
            results.emplace(
                current_id,
                core::TaskResult::create_failed(
                    current_id,
                    cat,
                    resp.exit_code(),
                    resp.os_error_code(),
                    resp.standard_output(),
                    resp.standard_error(),
                    resp.error_message(),
                    task_start,
                    task_end,
                    task_duration));
            execution_order.push_back(current_id);

            // 5. Pruned BFS block propagation to downstream transitive dependents
            std::vector<core::TaskId> block_queue;
            auto enqueue_blocked = [&](const core::TaskId& dep_id, const core::TaskId& source_id) {
                auto state = task_states[dep_id];

                // Pruning: skip already terminal nodes (do not re-expand or re-enqueue)
                if (state == core::TaskState::Blocked ||
                    state == core::TaskState::Failed ||
                    state == core::TaskState::Succeeded) {
                    return;
                }

                if (state == core::TaskState::Ready) {
                    throw std::logic_error(
                        "Internal scheduling invariant violated: task " +
                        dep_id.value() +
                        " was Ready when prerequisite failed.");
                }

                // Node must be Pending -> transition to Blocked immediately upon discovery
                task_states[dep_id] = core::TaskState::Blocked;
                results.emplace(
                    dep_id,
                    core::TaskResult::create_blocked(
                        dep_id,
                        "Prerequisite task " + source_id.value() + " failed"));

                block_queue.push_back(dep_id);
            };

            auto dep_it = dependents.find(current_id);
            if (dep_it != dependents.end()) {
                for (const auto& dep_id : dep_it->second) {
                    enqueue_blocked(dep_id, current_id);
                }
            }

            size_t b_idx = 0;
            while (b_idx < block_queue.size()) {
                core::TaskId blocked_id = block_queue[b_idx++];
                auto next_dep_it = dependents.find(blocked_id);
                if (next_dep_it != dependents.end()) {
                    for (const auto& next_dep_id : next_dep_it->second) {
                        enqueue_blocked(next_dep_id, blocked_id);
                    }
                }
            }
        }
    }

    // 6. Guarantee every task has an authoritative terminal outcome
    for (const auto& task : workflow.tasks()) {
        if (results.find(task.id()) == results.end()) {
            throw std::logic_error(
                "Internal scheduling invariant violated: task " +
                task.id().value() +
                " was not resolved to a terminal state.");
        }
    }

    auto end_time = std::chrono::system_clock::now();
    core::WorkflowExecution execution(
        workflow.name(),
        std::move(results),
        std::move(execution_order),
        start_time,
        end_time);

    return WorkflowRunResult::executed(std::move(execution));
}

} // namespace taskforge::scheduler
