#pragma once

#include "taskforge/core/task_id.hpp"

#include <optional>
#include <string>
#include <vector>

namespace taskforge::core {

enum class ValidationErrorCode {
    EmptyWorkflow,
    DuplicateTaskId,
    MissingDependency,
    SelfDependency,
    CircularDependency,
    EmptyExecutable,
    InvalidTaskId,
    JsonSyntaxError,
    InvalidSchema,
    DuplicateDependency,
    UnknownField,
    UnsupportedVersion
};

/**
 * @brief Represents a specific domain validation violation with rich context.
 */
struct ValidationError {
    ValidationErrorCode code;
    std::string message;
    std::optional<TaskId> task_id;
    std::vector<TaskId> cycle_path; // Populated for CircularDependency errors

    [[nodiscard]] std::string to_string() const;
};

/**
 * @brief Accumulator for all validation errors detected during workflow analysis.
 */
class ValidationResult {
public:
    ValidationResult() = default;

    /// @brief Appends an error to the result.
    void add_error(ValidationError error);

    /// @brief True if no errors were encountered.
    [[nodiscard]] bool is_valid() const noexcept { return errors_.empty(); }

    /// @brief Boolean conversion helper (true if valid).
    [[nodiscard]] explicit operator bool() const noexcept { return is_valid(); }

    /// @brief Returns the collection of validation errors.
    [[nodiscard]] const std::vector<ValidationError>& errors() const noexcept { return errors_; }

    /// @brief Checks if a specific error code was recorded.
    [[nodiscard]] bool has_error_code(ValidationErrorCode code) const noexcept;

    /// @brief Returns a formatted multi-line summary of all errors.
    [[nodiscard]] std::string format_summary() const;

private:
    std::vector<ValidationError> errors_;
};

} // namespace taskforge::core
