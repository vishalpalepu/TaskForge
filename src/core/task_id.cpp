#include "taskforge/core/task_id.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>

namespace taskforge::core {

bool TaskId::is_valid(std::string_view id) noexcept {
    if (id.empty()) {
        return false;
    }

    return std::ranges::all_of(id, [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) ||
               ch == '-' || ch == '_' || ch == '.';
    });
}

std::optional<TaskId> TaskId::try_create(std::string value) noexcept {
    if (!is_valid(value)) {
        return std::nullopt;
    }
    return TaskId{std::move(value)};
}

TaskId::TaskId(std::string value) : value_(std::move(value)) {
    if (!is_valid(value_)) {
        throw std::invalid_argument(
            "Invalid TaskId: '" + value_ +
            "'. Task IDs must be non-empty and contain only alphanumeric characters, '-', '_', or '.'.");
    }
}

std::ostream& operator<<(std::ostream& os, const TaskId& id) {
    return os << id.value();
}

} // namespace taskforge::core
