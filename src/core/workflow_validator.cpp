#include "taskforge/core/workflow_validator.hpp"

#include <unordered_set>

namespace taskforge::core {

ValidationResult WorkflowValidator::validate(const WorkflowDefinition& workflow) {
    ValidationResult result;

    // Rule 1: Non-empty workflow
    if (workflow.empty()) {
        result.add_error(ValidationError{
            .code = ValidationErrorCode::EmptyWorkflow,
            .message = "Workflow '" + workflow.name() + "' contains no tasks. Workflows must define at least one task.",
            .task_id = std::nullopt,
            .cycle_path = {}
        });
        return result; // Cannot validate dependencies or cycles on empty workflow
    }

    // Rule 2 & 3: Referential integrity & self-dependency
    for (const auto& task : workflow.tasks()) {
        if (task.executable().empty()) {
            result.add_error(ValidationError{
                .code = ValidationErrorCode::EmptyExecutable,
                .message = "Executable path cannot be empty.",
                .task_id = task.id(),
                .cycle_path = {}
            });
        }

        for (const auto& dep : task.dependencies()) {
            if (dep == task.id()) {
                result.add_error(ValidationError{
                    .code = ValidationErrorCode::SelfDependency,
                    .message = "Task cannot list itself as a prerequisite.",
                    .task_id = task.id(),
                    .cycle_path = {}
                });
            } else if (!workflow.has_task(dep)) {
                result.add_error(ValidationError{
                    .code = ValidationErrorCode::MissingDependency,
                    .message = "Prerequisite task '" + dep.value() + "' does not exist in the workflow.",
                    .task_id = task.id(),
                    .cycle_path = {}
                });
            }
        }
    }

    // Rule 4: Cycle detection
    DependencyGraph graph(workflow);
    if (graph.has_cycles()) {
        for (const auto& cycle : graph.detected_cycles()) {
            result.add_error(ValidationError{
                .code = ValidationErrorCode::CircularDependency,
                .message = "Circular dependency chain detected.",
                .task_id = cycle.empty() ? std::nullopt : std::optional<TaskId>(cycle.front()),
                .cycle_path = cycle
            });
        }
    }

    return result;
}

ValidationResult WorkflowValidator::validate_raw_tasks(
    std::string_view workflow_name,
    const std::vector<TaskDefinition>& tasks) {

    ValidationResult result;

    if (tasks.empty()) {
        result.add_error(ValidationError{
            .code = ValidationErrorCode::EmptyWorkflow,
            .message = "Workflow '" + std::string(workflow_name) + "' contains no tasks. Workflows must define at least one task.",
            .task_id = std::nullopt,
            .cycle_path = {}
        });
        return result;
    }

    // Check for duplicate task IDs
    std::unordered_set<TaskId> seen_ids;
    bool has_duplicates = false;
    for (const auto& task : tasks) {
        if (!seen_ids.insert(task.id()).second) {
            result.add_error(ValidationError{
                .code = ValidationErrorCode::DuplicateTaskId,
                .message = "Duplicate task ID '" + task.id().value() + "' found in workflow.",
                .task_id = task.id(),
                .cycle_path = {}
            });
            has_duplicates = true;
        }
    }

    // If duplicate IDs exist, we cannot safely construct a valid WorkflowDefinition
    if (has_duplicates) {
        return result;
    }

    // Construct WorkflowDefinition and run full graph validation
    try {
        auto workflow = WorkflowDefinition::create(std::string(workflow_name), tasks);
        return validate(workflow);
    } catch (const std::exception& ex) {
        result.add_error(ValidationError{
            .code = ValidationErrorCode::DuplicateTaskId,
            .message = ex.what(),
            .task_id = std::nullopt,
            .cycle_path = {}
        });
        return result;
    }
}

} // namespace taskforge::core
