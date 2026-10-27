#pragma once

#include "taskforge/core/validation_error.hpp"
#include "taskforge/core/workflow_definition.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace taskforge::manifest {

/**
 * @brief Utility for explicit UTF-8 string to native filesystem path conversion.
 *
 * Guarantees correct Unicode path handling on Windows without encoding corruption.
 */
[[nodiscard]] std::filesystem::path utf8_to_path(std::string_view utf8_str);

/**
 * @brief Converts a native filesystem path back to a UTF-8 encoded string.
 */
[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);

/**
 * @brief Authoritative discriminated outcome of manifest parsing.
 *
 * Enforces mutual exclusivity:
 * - Cannot represent success without a valid WorkflowDefinition.
 * - Cannot represent failure without diagnostic errors.
 */
class ManifestParseResult {
public:
    [[nodiscard]] static ManifestParseResult success(core::WorkflowDefinition workflow);
    [[nodiscard]] static ManifestParseResult failure(core::ValidationResult diagnostics);

    // Regular value semantics
    ManifestParseResult(const ManifestParseResult&) = default;
    ManifestParseResult& operator=(const ManifestParseResult&) = default;
    ManifestParseResult(ManifestParseResult&&) noexcept = default;
    ManifestParseResult& operator=(ManifestParseResult&&) noexcept = default;
    ~ManifestParseResult() = default;

    /// @brief True if JSON parsed successfully and all domain and semantic rules passed.
    [[nodiscard]] bool is_valid() const noexcept;

    /// @brief Explicit boolean conversion operator (true if valid).
    [[nodiscard]] explicit operator bool() const noexcept { return is_valid(); }

    /// @brief Accesses the validated WorkflowDefinition.
    /// @throws std::logic_error if is_valid() is false.
    [[nodiscard]] const core::WorkflowDefinition& workflow() const;

    /// @brief Accesses the validation and schema diagnostics.
    /// @throws std::logic_error if is_valid() is true.
    [[nodiscard]] const core::ValidationResult& diagnostics() const;

private:
    explicit ManifestParseResult(core::WorkflowDefinition workflow);
    explicit ManifestParseResult(core::ValidationResult diagnostics);

    std::variant<core::ValidationResult, core::WorkflowDefinition> result_;
};

/**
 * @brief Transforms untrusted JSON manifest input into validated domain workflows.
 *
 * Follows a multi-phase validation strategy:
 * 1. Syntax check: reports JSON syntax errors with line and column numbers.
 * 2. Schema check: accumulates field type, missing field, and unknown field errors.
 * 3. Local constraint check: validates TaskId syntax, executables, intra-task duplicates.
 * 4. Domain construction: creates domain instances ONLY after phases 1-3 succeed.
 * 5. Semantic validation: runs WorkflowValidator (referential integrity, cycles).
 */
class WorkflowManifestParser {
public:
    /// @brief Parses a workflow from a UTF-8 JSON text string.
    [[nodiscard]] static ManifestParseResult parse_string(std::string_view json_content);

    /// @brief Reads and parses a workflow from a manifest file on disk.
    /// @return std::nullopt if the file does not exist or cannot be read.
    [[nodiscard]] static std::optional<ManifestParseResult> parse_file(const std::filesystem::path& path);
};

} // namespace taskforge::manifest
