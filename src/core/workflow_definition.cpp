#include "taskforge/core/workflow_definition.hpp"

#include <stdexcept>

namespace taskforge::core {

WorkflowDefinition WorkflowDefinition::create(
    std::string name,
    std::vector<TaskDefinition> tasks) {

    std::unordered_map<TaskId, size_t> lookup;
    lookup.reserve(tasks.size());

    for (size_t i = 0; i < tasks.size(); ++i) {
        // Enforce declaration order index
        tasks[i].set_declaration_index(i);

        const auto& id = tasks[i].id();
        auto [_, inserted] = lookup.emplace(id, i);
        if (!inserted) {
            throw std::invalid_argument(
                "Duplicate task ID '" + id.value() + "' in workflow '" + name + "'.");
        }
    }

    return WorkflowDefinition{std::move(name), std::move(tasks), std::move(lookup)};
}

WorkflowDefinition::WorkflowDefinition(
    std::string name,
    std::vector<TaskDefinition> tasks,
    std::unordered_map<TaskId, size_t> lookup)
    : name_(std::move(name)),
      tasks_(std::move(tasks)),
      lookup_(std::move(lookup)) {}

bool WorkflowDefinition::has_task(const TaskId& id) const noexcept {
    return lookup_.contains(id);
}

const TaskDefinition* WorkflowDefinition::find_task(const TaskId& id) const noexcept {
    auto it = lookup_.find(id);
    if (it == lookup_.end()) {
        return nullptr;
    }
    return &tasks_[it->second];
}

size_t WorkflowDefinition::declaration_index_of(const TaskId& id) const {
    auto it = lookup_.find(id);
    if (it == lookup_.end()) {
        throw std::out_of_range("Task '" + id.value() + "' not found in workflow.");
    }
    return it->second;
}

} // namespace taskforge::core
