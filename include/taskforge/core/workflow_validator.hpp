#pragma once

#include "taskforge/core/dependency_graph.hpp"
#include "taskforge/core/validation_error.hpp"
#include "taskforge/core/workflow_definition.hpp"

namespace taskforge::core {

/**
 * @brief Semantic validator for TaskForge workflow definitions.
 *
 * Verifies workflow-level invariants on an established WorkflowDefinition:
 * 1. Non-empty workflow (> 0 tasks).
 * 2. Referential integrity (every declared prerequisite must exist in the workflow).
 * 3. Global acyclicity (no circular dependency loops).
 *
 * Local task invariants (non-empty executable, no duplicate dependencies, no self-dependency)
 * are enforced by TaskDefinition::create(). Aggregate invariants (unique task IDs) are enforced
 * by WorkflowDefinition::create().
 */
class WorkflowValidator {
public:
    /// @brief Validates an established WorkflowDefinition against workflow-level semantic rules.
    [[nodiscard]] static ValidationResult validate(const WorkflowDefinition& workflow);
};

} // namespace taskforge::core
