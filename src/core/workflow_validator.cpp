#include "taskforge/core/workflow_validator.hpp"

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

    // Rule 2: Referential integrity (all declared prerequisites must exist in the workflow)
    for (const auto& task : workflow.tasks()) {
        for (const auto& dep : task.dependencies()) {
            if (!workflow.has_task(dep)) {
                result.add_error(ValidationError{
                    .code = ValidationErrorCode::MissingDependency,
                    .message = "Prerequisite task '" + dep.value() + "' does not exist in the workflow.",
                    .task_id = task.id(),
                    .cycle_path = {}
                });
            }
        }
    }

    // Rule 3: Global acyclicity (no circular dependency loops)
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

} // namespace taskforge::core
