#pragma once

#include <compare>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace taskforge::core {

/**
 * @brief Validated, strongly-typed identifier representing a unique task within a workflow.
 *
 * Invariant: Non-empty, consisting only of alphanumeric characters, hyphens, underscores, or dots.
 * [a-zA-Z0-9_\-\.]+
 *
 * Implements regular value semantics with zero heap allocation beyond internal string storage.
 *
 * Moved-From Contract:
 * - A moved-from TaskId is in a valid but unspecified state (typically holding an empty string).
 * - It must not be assumed to satisfy is_valid() and should only be reassigned or destroyed.
 */
class TaskId {
public:
    /// @brief Checks whether the given string conforms to valid TaskId syntax without throwing.
    [[nodiscard]] static bool is_valid(std::string_view id) noexcept;

    /// @brief Attempts to create a TaskId, returning std::nullopt if the string is invalid.
    [[nodiscard]] static std::optional<TaskId> try_create(std::string value) noexcept;

    /// @brief Constructs a TaskId from a string, throwing std::invalid_argument if invalid.
    explicit TaskId(std::string value);

    // Default value operations (ADR 0001)
    TaskId(const TaskId&) = default;
    TaskId& operator=(const TaskId&) = default;
    TaskId(TaskId&&) noexcept = default;
    TaskId& operator=(TaskId&&) noexcept = default;
    ~TaskId() = default;

    /// @brief Returns the underlying identifier string.
    [[nodiscard]] const std::string& value() const noexcept { return value_; }

    /// @brief Returns a string_view of the identifier.
    [[nodiscard]] std::string_view view() const noexcept { return value_; }

    /// @brief Returns a const char* pointer for C-string compatibility.
    [[nodiscard]] const char* c_str() const noexcept { return value_.c_str(); }

    /// @brief C++20 three-way comparison operator for deterministic lexicographical ordering.
    auto operator<=>(const TaskId& other) const noexcept = default;
    bool operator==(const TaskId& other) const noexcept = default;

    friend std::ostream& operator<<(std::ostream& os, const TaskId& id);

private:
    std::string value_;
};

} // namespace taskforge::core

// Hash support for std::unordered_map and std::unordered_set
template <>
struct std::hash<taskforge::core::TaskId> {
    size_t operator()(const taskforge::core::TaskId& id) const noexcept {
        return std::hash<std::string>{}(id.value());
    }
};
