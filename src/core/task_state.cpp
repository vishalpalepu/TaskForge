#include "taskforge/core/task_state.hpp"

#include <iostream>

namespace taskforge::core {

std::string_view to_string(TaskState state) noexcept {
    switch (state) {
        case TaskState::Pending:
            return "Pending";
        case TaskState::Ready:
            return "Ready";
        case TaskState::Running:
            return "Running";
        case TaskState::Succeeded:
            return "Succeeded";
        case TaskState::Failed:
            return "Failed";
        case TaskState::Blocked:
            return "Blocked";
    }
    return "Unknown";
}

std::string_view to_string(FailureCategory category) noexcept {
    switch (category) {
        case FailureCategory::None:
            return "None";
        case FailureCategory::LaunchError:
            return "LaunchError";
        case FailureCategory::NonZeroExit:
            return "NonZeroExit";
        case FailureCategory::SystemFault:
            return "SystemFault";
        case FailureCategory::Timeout:
            return "Timeout";
    }
    return "Unknown";
}

std::ostream& operator<<(std::ostream& os, TaskState state) {
    return os << to_string(state);
}

std::ostream& operator<<(std::ostream& os, FailureCategory category) {
    return os << to_string(category);
}

} // namespace taskforge::core
