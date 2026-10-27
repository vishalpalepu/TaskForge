#pragma once

#include "taskforge/core/task_id.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace taskforge::core {

/**
 * @brief Immutable value object describing a single unit of execution.
 *
 * Adheres to ADR 0001:
 * - Uses value semantics with private non-const members to enable efficient moves and vector reallocation.
 * - Enforces invariants upon construction.
 * - Exposes only const accessors to external callers.
 * - Stores declaration_index for deterministic scheduling (ADR 0002).
 */
class TaskDefinition {
public:
    /// @brief Factory function that validates invariants and constructs a TaskDefinition.
    /// @throws std::invalid_argument if executable is empty or self-dependency detected.
    [[nodiscard]] static TaskDefinition create(
        TaskId id,
        std::string description,
        std::filesystem::path executable,
        std::vector<std::string> arguments = {},
        std::optional<std::filesystem::path> working_directory = std::nullopt,
        std::vector<TaskId> dependencies = {},
        size_t declaration_index = 0);

    // Standard value semantics (ADR 0001)
    TaskDefinition(const TaskDefinition&) = default;
    TaskDefinition& operator=(const TaskDefinition&) = default;
    TaskDefinition(TaskDefinition&&) noexcept = default;
    TaskDefinition& operator=(TaskDefinition&&) noexcept = default;
    ~TaskDefinition() = default;

    // Const-only observers
    [[nodiscard]] const TaskId& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& description() const noexcept { return description_; }
    [[nodiscard]] const std::filesystem::path& executable() const noexcept { return executable_; }
    [[nodiscard]] const std::vector<std::string>& arguments() const noexcept { return arguments_; }
    [[nodiscard]] const std::optional<std::filesystem::path>& working_directory() const noexcept {
        return working_directory_;
    }
    [[nodiscard]] const std::vector<TaskId>& dependencies() const noexcept { return dependencies_; }
    [[nodiscard]] size_t declaration_index() const noexcept { return declaration_index_; }

    /// @brief Checks if this task depends directly on prerequisite_id.
    [[nodiscard]] bool depends_on(const TaskId& prerequisite_id) const noexcept;

    /// @brief Internal helper used by WorkflowDefinition to establish declaration ordering.
    void set_declaration_index(size_t index) noexcept { declaration_index_ = index; }

private:
    TaskDefinition(
        TaskId id,
        std::string description,
        std::filesystem::path executable,
        std::vector<std::string> arguments,
        std::optional<std::filesystem::path> working_directory,
        std::vector<TaskId> dependencies,
        size_t declaration_index);

    TaskId id_;
    std::string description_;
    std::filesystem::path executable_;
    std::vector<std::string> arguments_;
    std::optional<std::filesystem::path> working_directory_;
    std::vector<TaskId> dependencies_;
    size_t declaration_index_{0};
};

} // namespace taskforge::core
