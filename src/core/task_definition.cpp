#include "taskforge/core/task_definition.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace taskforge::core {

TaskDefinition TaskDefinition::create(
    TaskId id,
    std::string description,
    std::filesystem::path executable,
    std::vector<std::string> arguments,
    std::optional<std::filesystem::path> working_directory,
    std::vector<TaskId> dependencies,
    size_t declaration_index) {

    if (executable.empty()) {
        throw std::invalid_argument(
            "Task '" + id.value() + "': executable path must not be empty.");
    }

    // Invariant: cannot depend on itself
    for (const auto& dep : dependencies) {
        if (dep == id) {
            throw std::invalid_argument(
                "Task '" + id.value() + "': self-dependency detected. A task cannot depend on itself.");
        }
    }

    // Invariant: dependencies list should not contain duplicates
    std::unordered_set<TaskId> seen_deps;
    for (const auto& dep : dependencies) {
        if (!seen_deps.insert(dep).second) {
            throw std::invalid_argument(
                "Task '" + id.value() + "': duplicate prerequisite '" + dep.value() + "' listed in dependencies.");
        }
    }

    return TaskDefinition{
        std::move(id),
        std::move(description),
        std::move(executable),
        std::move(arguments),
        std::move(working_directory),
        std::move(dependencies),
        declaration_index};
}

TaskDefinition::TaskDefinition(
    TaskId id,
    std::string description,
    std::filesystem::path executable,
    std::vector<std::string> arguments,
    std::optional<std::filesystem::path> working_directory,
    std::vector<TaskId> dependencies,
    size_t declaration_index)
    : id_(std::move(id)),
      description_(std::move(description)),
      executable_(std::move(executable)),
      arguments_(std::move(arguments)),
      working_directory_(std::move(working_directory)),
      dependencies_(std::move(dependencies)),
      declaration_index_(declaration_index) {}

bool TaskDefinition::depends_on(const TaskId& prerequisite_id) const noexcept {
    return std::ranges::any_of(dependencies_, [&prerequisite_id](const TaskId& dep) {
        return dep == prerequisite_id;
    });
}

} // namespace taskforge::core
