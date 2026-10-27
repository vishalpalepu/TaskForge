#pragma once

#include "taskforge/core/dependency_graph.hpp"
#include "taskforge/core/validation_error.hpp"
#include "taskforge/core/workflow_definition.hpp"

#include <string_view>
#include <vector>

namespace taskforge::core {

/**
 * @brief Comprehensive semantic validator for TaskForge workflows.
 *
 * Verifies all pre-execution domain invariants:
 * 1. Non-empty workflow.
 * 2. Uniqueness of task IDs (no duplicate IDs).
 * 3. Referential integrity (no references to nonexistent prerequisites).
 * 4. Self-dependency absence.
 * 5. Acyclicity (no circular dependency loops).
 * 6. Non-empty executable paths.
 */
class WorkflowValidator {
public:
    /// @brief Validates an established WorkflowDefinition.
    [[nodiscard]] static ValidationResult validate(const WorkflowDefinition& workflow);

    /// @brief Validates a raw collection of tasks prior to constructing a WorkflowDefinition.
    /// This allows detecting duplicate task IDs as part of a ValidationResult report.
    [[nodiscard]] static ValidationResult validate_raw_tasks(
        std::string_view workflow_name,
        const std::vector<TaskDefinition>& tasks);
};

} // namespace taskforge::core
