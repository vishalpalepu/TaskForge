#include "taskforge/core/workflow_execution.hpp"

#include <algorithm>
#include <stdexcept>

namespace taskforge::core {

WorkflowExecution::WorkflowExecution(
    std::string workflow_name,
    std::unordered_map<TaskId, TaskResult> results,
    std::vector<TaskId> execution_order,
    std::chrono::system_clock::time_point start_time,
    std::chrono::system_clock::time_point end_time)
    : workflow_name_(std::move(workflow_name)),
      results_(std::move(results)),
      execution_order_(std::move(execution_order)),
      start_time_(start_time),
      end_time_(end_time) {}

TaskState WorkflowExecution::task_state(const TaskId& id) const {
    auto it = results_.find(id);
    if (it == results_.end()) {
        throw std::out_of_range("Task not found in workflow execution results: " + id.value());
    }
    return it->second.final_state();
}

const TaskResult* WorkflowExecution::task_result(const TaskId& id) const noexcept {
    auto it = results_.find(id);
    if (it == results_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool WorkflowExecution::is_successful() const noexcept {
    if (results_.empty()) {
        return false;
    }
    return std::all_of(results_.begin(), results_.end(), [](const auto& pair) {
        return pair.second.final_state() == TaskState::Succeeded;
    });
}

bool WorkflowExecution::has_failures() const noexcept {
    return std::any_of(results_.begin(), results_.end(), [](const auto& pair) {
        return pair.second.final_state() == TaskState::Failed ||
               pair.second.final_state() == TaskState::Blocked;
    });
}

std::chrono::milliseconds WorkflowExecution::total_duration() const noexcept {
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end_time_ - start_time_);
    return dur < std::chrono::milliseconds{0} ? std::chrono::milliseconds{0} : dur;
}

size_t WorkflowExecution::count_with_state(TaskState state) const noexcept {
    return static_cast<size_t>(std::count_if(results_.begin(), results_.end(), [state](const auto& pair) {
        return pair.second.final_state() == state;
    }));
}

} // namespace taskforge::core
