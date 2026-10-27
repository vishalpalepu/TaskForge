#include "taskforge/core/validation_error.hpp"

#include <algorithm>
#include <sstream>

namespace taskforge::core {

std::string ValidationError::to_string() const {
    std::ostringstream oss;
    switch (code) {
        case ValidationErrorCode::EmptyWorkflow:
            oss << "[EMPTY_WORKFLOW] ";
            break;
        case ValidationErrorCode::DuplicateTaskId:
            oss << "[DUPLICATE_TASK_ID] ";
            break;
        case ValidationErrorCode::MissingDependency:
            oss << "[MISSING_DEPENDENCY] ";
            break;
        case ValidationErrorCode::SelfDependency:
            oss << "[SELF_DEPENDENCY] ";
            break;
        case ValidationErrorCode::CircularDependency:
            oss << "[CIRCULAR_DEPENDENCY] ";
            break;
        case ValidationErrorCode::EmptyExecutable:
            oss << "[EMPTY_EXECUTABLE] ";
            break;
        case ValidationErrorCode::InvalidTaskId:
            oss << "[INVALID_TASK_ID] ";
            break;
    }

    if (task_id) {
        oss << "Task '" << task_id->value() << "': ";
    }
    oss << message;

    if (!cycle_path.empty()) {
        oss << " (Cycle: ";
        for (size_t i = 0; i < cycle_path.size(); ++i) {
            oss << cycle_path[i].value();
            if (i + 1 < cycle_path.size()) {
                oss << " -> ";
            }
        }
        oss << ")";
    }

    return oss.str();
}

void ValidationResult::add_error(ValidationError error) {
    errors_.push_back(std::move(error));
}

bool ValidationResult::has_error_code(ValidationErrorCode code) const noexcept {
    return std::ranges::any_of(errors_, [code](const ValidationError& err) {
        return err.code == code;
    });
}

std::string ValidationResult::format_summary() const {
    if (is_valid()) {
        return "Validation successful: 0 errors.";
    }

    std::ostringstream oss;
    oss << "Validation failed with " << errors_.size() << " error(s):\n";
    for (size_t i = 0; i < errors_.size(); ++i) {
        oss << "  " << (i + 1) << ". " << errors_[i].to_string() << "\n";
    }
    return oss.str();
}

} // namespace taskforge::core
