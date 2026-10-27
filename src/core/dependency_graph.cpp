#include "taskforge/core/dependency_graph.hpp"

#include <algorithm>

namespace taskforge::core {

const std::vector<TaskId> DependencyGraph::empty_task_list_{};

DependencyGraph::DependencyGraph(const WorkflowDefinition& workflow) {
    build_graph(workflow);
    detect_cycles(workflow);
}

void DependencyGraph::build_graph(const WorkflowDefinition& workflow) {
    // 1. Initialize all vertices
    for (const auto& task : workflow.tasks()) {
        const auto& id = task.id();
        forward_edges_[id] = {};
        backward_edges_[id] = {};
        in_degrees_[id] = task.dependencies().size();
    }

    // 2. Populate edges
    for (const auto& task : workflow.tasks()) {
        const auto& task_id = task.id();
        for (const auto& dep_id : task.dependencies()) {
            backward_edges_[task_id].push_back(dep_id);

            // If the prerequisite exists in the workflow, record forward edge
            if (workflow.has_task(dep_id)) {
                forward_edges_[dep_id].push_back(task_id);
            }
        }
    }
}

void DependencyGraph::detect_cycles(const WorkflowDefinition& workflow) {
    std::unordered_map<TaskId, NodeColor> colors;
    colors.reserve(workflow.task_count());

    for (const auto& task : workflow.tasks()) {
        colors.emplace(task.id(), NodeColor::White);
    }

    std::vector<TaskId> call_stack;
    call_stack.reserve(workflow.task_count());

    // Traverse in deterministic manifest declaration order (ADR 0002)
    for (const auto& task : workflow.tasks()) {
        const auto& id = task.id();
        if (colors[id] == NodeColor::White) {
            dfs_visit(id, colors, call_stack);
        }
    }
}

void DependencyGraph::dfs_visit(
    const TaskId& current,
    std::unordered_map<TaskId, NodeColor>& colors,
    std::vector<TaskId>& call_stack) {

    colors[current] = NodeColor::Gray;
    call_stack.push_back(current);

    auto it = backward_edges_.find(current);
    if (it != backward_edges_.end()) {
        for (const auto& dep : it->second) {
            auto color_it = colors.find(dep);
            if (color_it == colors.end()) {
                // Dependency does not exist in workflow; handled by WorkflowValidator
                continue;
            }

            if (color_it->second == NodeColor::Gray) {
                // Cycle detected: dep is an active ancestor on recursion stack
                auto stack_it = std::find(call_stack.begin(), call_stack.end(), dep);
                if (stack_it != call_stack.end()) {
                    std::vector<TaskId> cycle(stack_it, call_stack.end());
                    cycle.push_back(dep); // Close loop: [dep, ..., current, dep]
                    detected_cycles_.push_back(std::move(cycle));
                }
            } else if (color_it->second == NodeColor::White) {
                dfs_visit(dep, colors, call_stack);
            }
        }
    }

    call_stack.pop_back();
    colors[current] = NodeColor::Black;
}

const std::vector<TaskId>& DependencyGraph::dependents_of(const TaskId& id) const {
    auto it = forward_edges_.find(id);
    if (it != forward_edges_.end()) {
        return it->second;
    }
    return empty_task_list_;
}

const std::vector<TaskId>& DependencyGraph::prerequisites_of(const TaskId& id) const {
    auto it = backward_edges_.find(id);
    if (it != backward_edges_.end()) {
        return it->second;
    }
    return empty_task_list_;
}

size_t DependencyGraph::in_degree(const TaskId& id) const {
    auto it = in_degrees_.find(id);
    if (it != in_degrees_.end()) {
        return it->second;
    }
    return 0;
}

std::vector<TaskId> DependencyGraph::root_tasks() const {
    std::vector<TaskId> roots;
    for (const auto& [id, deg] : in_degrees_) {
        if (deg == 0) {
            roots.push_back(id);
        }
    }
    return roots;
}

} // namespace taskforge::core
