#include "taskforge/manifest/manifest_parser.hpp"
#include "taskforge/core/workflow_validator.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace taskforge::manifest {

// ---------------------------------------------------------------------------
// UTF-8 <-> Native path conversion
// ---------------------------------------------------------------------------

std::filesystem::path utf8_to_path(std::string_view utf8_str) {
#if defined(_WIN32)
    return std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(utf8_str.data()), utf8_str.size()));
#else
    return std::filesystem::path(utf8_str);
#endif
}

std::string path_to_utf8(const std::filesystem::path& path) {
    auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// ---------------------------------------------------------------------------
// ManifestParseResult implementation
// ---------------------------------------------------------------------------

ManifestParseResult ManifestParseResult::success(core::WorkflowDefinition workflow) {
    return ManifestParseResult(std::move(workflow));
}

ManifestParseResult ManifestParseResult::failure(core::ValidationResult diagnostics) {
    if (diagnostics.is_valid()) {
        throw std::invalid_argument(
            "Cannot create a failure ManifestParseResult with valid diagnostics.");
    }
    return ManifestParseResult(std::move(diagnostics));
}

ManifestParseResult::ManifestParseResult(core::WorkflowDefinition workflow)
    : result_(std::move(workflow)) {}

ManifestParseResult::ManifestParseResult(core::ValidationResult diagnostics)
    : result_(std::move(diagnostics)) {}

bool ManifestParseResult::is_valid() const noexcept {
    return std::holds_alternative<core::WorkflowDefinition>(result_);
}

const core::WorkflowDefinition& ManifestParseResult::workflow() const {
    if (const auto* wf = std::get_if<core::WorkflowDefinition>(&result_)) {
        return *wf;
    }
    throw std::logic_error(
        "ManifestParseResult::workflow() called on an invalid parse result.");
}

const core::ValidationResult& ManifestParseResult::diagnostics() const {
    if (const auto* diag = std::get_if<core::ValidationResult>(&result_)) {
        return *diag;
    }
    throw std::logic_error(
        "ManifestParseResult::diagnostics() called on a valid parse result.");
}

// ---------------------------------------------------------------------------
// WorkflowManifestParser implementation
// ---------------------------------------------------------------------------

ManifestParseResult WorkflowManifestParser::parse_string(std::string_view json_content) {
    // Phase 1: Syntactic parsing with nlohmann::json
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_content);
    } catch (const nlohmann::json::parse_error& e) {
        core::ValidationResult syntax_diagnostics;
        syntax_diagnostics.add_error(core::ValidationError{
            core::ValidationErrorCode::JsonSyntaxError,
            e.what(),
            std::nullopt,
            {}});
        return ManifestParseResult::failure(std::move(syntax_diagnostics));
    }

    core::ValidationResult diagnostics;

    // Phase 2: Schema structure validation
    if (!root.is_object()) {
        diagnostics.add_error(core::ValidationError{
            core::ValidationErrorCode::InvalidSchema,
            "Manifest root must be a JSON object.",
            std::nullopt,
            {}});
        return ManifestParseResult::failure(std::move(diagnostics));
    }

    // Check unknown root fields
    const std::unordered_set<std::string> allowed_root_keys = {"version", "name", "tasks"};
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!allowed_root_keys.contains(it.key())) {
            diagnostics.add_error(core::ValidationError{
                core::ValidationErrorCode::UnknownField,
                "Unrecognized field '" + it.key() + "' in manifest root.",
                std::nullopt,
                {}});
        }
    }

    // Version validation (strict: must be string "1.0" if present)
    if (root.contains("version")) {
        if (!root["version"].is_string()) {
            diagnostics.add_error(core::ValidationError{
                core::ValidationErrorCode::InvalidSchema,
                "Field 'version' must be a string.",
                std::nullopt,
                {}});
        } else {
            std::string ver = root["version"].get<std::string>();
            if (ver != "1.0") {
                diagnostics.add_error(core::ValidationError{
                    core::ValidationErrorCode::UnsupportedVersion,
                    "Unsupported manifest version '" + ver + "', expected '1.0'.",
                    std::nullopt,
                    {}});
            }
        }
    }

    // Workflow name validation
    std::string workflow_name;
    if (!root.contains("name")) {
        diagnostics.add_error(core::ValidationError{
            core::ValidationErrorCode::InvalidSchema,
            "Required field 'name' is missing.",
            std::nullopt,
            {}});
    } else if (!root["name"].is_string()) {
        diagnostics.add_error(core::ValidationError{
            core::ValidationErrorCode::InvalidSchema,
            "Field 'name' must be a string.",
            std::nullopt,
            {}});
    } else {
        workflow_name = root["name"].get<std::string>();
        if (workflow_name.empty()) {
            diagnostics.add_error(core::ValidationError{
                core::ValidationErrorCode::InvalidSchema,
                "Workflow name cannot be empty.",
                std::nullopt,
                {}});
        }
    }

    // Tasks array validation
    if (!root.contains("tasks")) {
        diagnostics.add_error(core::ValidationError{
            core::ValidationErrorCode::InvalidSchema,
            "Required field 'tasks' is missing.",
            std::nullopt,
            {}});
    } else if (!root["tasks"].is_array()) {
        diagnostics.add_error(core::ValidationError{
            core::ValidationErrorCode::InvalidSchema,
            "Field 'tasks' must be an array.",
            std::nullopt,
            {}});
    } else if (root["tasks"].empty()) {
        diagnostics.add_error(core::ValidationError{
            core::ValidationErrorCode::EmptyWorkflow,
            "Workflow must contain at least one task.",
            std::nullopt,
            {}});
    }

    // Phase 3: Raw tasks ingestion and local task validation
    struct RawTaskInput {
        size_t index{0};
        std::string id;
        std::string description;
        std::string executable;
        std::vector<std::string> arguments;
        std::optional<std::string> working_directory;
        std::vector<std::string> dependencies;
    };

    std::vector<RawTaskInput> raw_tasks;
    std::unordered_set<std::string> seen_task_ids;

    if (root.contains("tasks") && root["tasks"].is_array()) {
        const std::unordered_set<std::string> allowed_task_keys = {
            "id", "description", "executable", "arguments", "working_directory", "dependencies"};

        size_t task_index = 0;
        for (const auto& task_json : root["tasks"]) {
            RawTaskInput raw_task;
            raw_task.index = task_index++;

            if (!task_json.is_object()) {
                diagnostics.add_error(core::ValidationError{
                    core::ValidationErrorCode::InvalidSchema,
                    "Task at index " + std::to_string(raw_task.index) + " must be a JSON object.",
                    std::nullopt,
                    {}});
                continue;
            }

            // Check unknown fields in task object
            for (auto it = task_json.begin(); it != task_json.end(); ++it) {
                if (!allowed_task_keys.contains(it.key())) {
                    diagnostics.add_error(core::ValidationError{
                        core::ValidationErrorCode::UnknownField,
                        "Unrecognized field '" + it.key() + "' in task at index " +
                            std::to_string(raw_task.index) + ".",
                        std::nullopt,
                        {}});
                }
            }

            // Task id
            std::optional<core::TaskId> task_id_opt;
            if (!task_json.contains("id")) {
                diagnostics.add_error(core::ValidationError{
                    core::ValidationErrorCode::InvalidSchema,
                    "Task at index " + std::to_string(raw_task.index) + " is missing required field 'id'.",
                    std::nullopt,
                    {}});
            } else if (!task_json["id"].is_string()) {
                diagnostics.add_error(core::ValidationError{
                    core::ValidationErrorCode::InvalidSchema,
                    "Task at index " + std::to_string(raw_task.index) + " field 'id' must be a string.",
                    std::nullopt,
                    {}});
            } else {
                std::string id_str = task_json["id"].get<std::string>();
                if (!core::TaskId::is_valid(id_str)) {
                    diagnostics.add_error(core::ValidationError{
                        core::ValidationErrorCode::InvalidTaskId,
                        "Invalid task ID '" + id_str + "' at index " +
                            std::to_string(raw_task.index) +
                            " (must be non-empty and contain only alphanumeric characters, '-', '_', or '.').",
                        std::nullopt,
                        {}});
                } else {
                    raw_task.id = id_str;
                    task_id_opt = core::TaskId(id_str);

                    if (seen_task_ids.contains(id_str)) {
                        diagnostics.add_error(core::ValidationError{
                            core::ValidationErrorCode::DuplicateTaskId,
                            "Duplicate task ID '" + id_str + "' declared at index " +
                                std::to_string(raw_task.index) + ".",
                            task_id_opt,
                            {}});
                    } else {
                        seen_task_ids.insert(id_str);
                    }
                }
            }

            // Description (optional)
            if (task_json.contains("description")) {
                if (!task_json["description"].is_string()) {
                    diagnostics.add_error(core::ValidationError{
                        core::ValidationErrorCode::InvalidSchema,
                        "Task field 'description' must be a string.",
                        task_id_opt,
                        {}});
                } else {
                    raw_task.description = task_json["description"].get<std::string>();
                }
            }

            // Executable (required, non-empty)
            if (!task_json.contains("executable")) {
                diagnostics.add_error(core::ValidationError{
                    core::ValidationErrorCode::InvalidSchema,
                    "Task is missing required field 'executable'.",
                    task_id_opt,
                    {}});
            } else if (!task_json["executable"].is_string()) {
                diagnostics.add_error(core::ValidationError{
                    core::ValidationErrorCode::InvalidSchema,
                    "Task field 'executable' must be a string.",
                    task_id_opt,
                    {}});
            } else {
                std::string exec_str = task_json["executable"].get<std::string>();
                if (exec_str.empty()) {
                    diagnostics.add_error(core::ValidationError{
                        core::ValidationErrorCode::EmptyExecutable,
                        "Task executable path cannot be empty.",
                        task_id_opt,
                        {}});
                } else {
                    raw_task.executable = exec_str;
                }
            }

            // Arguments (optional, array of strings)
            if (task_json.contains("arguments")) {
                if (!task_json["arguments"].is_array()) {
                    diagnostics.add_error(core::ValidationError{
                        core::ValidationErrorCode::InvalidSchema,
                        "Task field 'arguments' must be an array of strings.",
                        task_id_opt,
                        {}});
                } else {
                    size_t arg_idx = 0;
                    for (const auto& arg : task_json["arguments"]) {
                        if (!arg.is_string()) {
                            diagnostics.add_error(core::ValidationError{
                                core::ValidationErrorCode::InvalidSchema,
                                "Task argument at index " + std::to_string(arg_idx) + " must be a string.",
                                task_id_opt,
                                {}});
                        } else {
                            raw_task.arguments.push_back(arg.get<std::string>());
                        }
                        ++arg_idx;
                    }
                }
            }

            // Working directory (optional, string or null)
            if (task_json.contains("working_directory")) {
                if (!task_json["working_directory"].is_null() && !task_json["working_directory"].is_string()) {
                    diagnostics.add_error(core::ValidationError{
                        core::ValidationErrorCode::InvalidSchema,
                        "Task field 'working_directory' must be a string or null.",
                        task_id_opt,
                        {}});
                } else if (task_json["working_directory"].is_string()) {
                    raw_task.working_directory = task_json["working_directory"].get<std::string>();
                }
            }

            // Dependencies (optional, array of unique TaskId strings)
            if (task_json.contains("dependencies")) {
                if (!task_json["dependencies"].is_array()) {
                    diagnostics.add_error(core::ValidationError{
                        core::ValidationErrorCode::InvalidSchema,
                        "Task field 'dependencies' must be an array of strings.",
                        task_id_opt,
                        {}});
                } else {
                    std::unordered_set<std::string> seen_intra_deps;
                    size_t dep_idx = 0;
                    for (const auto& dep : task_json["dependencies"]) {
                        if (!dep.is_string()) {
                            diagnostics.add_error(core::ValidationError{
                                core::ValidationErrorCode::InvalidSchema,
                                "Task dependency at index " + std::to_string(dep_idx) + " must be a string.",
                                task_id_opt,
                                {}});
                        } else {
                            std::string dep_str = dep.get<std::string>();
                            if (!core::TaskId::is_valid(dep_str)) {
                                diagnostics.add_error(core::ValidationError{
                                    core::ValidationErrorCode::InvalidTaskId,
                                    "Invalid prerequisite task ID '" + dep_str + "' in dependencies.",
                                    task_id_opt,
                                    {}});
                            } else if (!raw_task.id.empty() && dep_str == raw_task.id) {
                                diagnostics.add_error(core::ValidationError{
                                    core::ValidationErrorCode::SelfDependency,
                                    "Task cannot depend on itself.",
                                    task_id_opt,
                                    {}});
                            } else if (seen_intra_deps.contains(dep_str)) {
                                diagnostics.add_error(core::ValidationError{
                                    core::ValidationErrorCode::DuplicateDependency,
                                    "Duplicate dependency '" + dep_str + "' declared in task.",
                                    task_id_opt,
                                    {}});
                            } else {
                                seen_intra_deps.insert(dep_str);
                                raw_task.dependencies.push_back(dep_str);
                            }
                        }
                        ++dep_idx;
                    }
                }
            }

            raw_tasks.push_back(std::move(raw_task));
        }
    }

    // Phase 4: Invariant Gate
    // If any structural, typing, or local invariant errors were found, abort prior to domain construction.
    if (!diagnostics.is_valid()) {
        return ManifestParseResult::failure(std::move(diagnostics));
    }

    // Construct domain objects (guaranteed safe by preceding validation)
    std::vector<core::TaskDefinition> tasks;
    tasks.reserve(raw_tasks.size());

    for (auto& raw : raw_tasks) {
        std::vector<core::TaskId> deps;
        deps.reserve(raw.dependencies.size());
        for (const auto& d : raw.dependencies) {
            deps.push_back(core::TaskId(d));
        }

        std::optional<std::filesystem::path> wd = std::nullopt;
        if (raw.working_directory) {
            wd = utf8_to_path(*raw.working_directory);
        }

        tasks.push_back(core::TaskDefinition::create(
            core::TaskId(raw.id),
            std::move(raw.description),
            utf8_to_path(raw.executable),
            std::move(raw.arguments),
            std::move(wd),
            std::move(deps)));
    }

    core::WorkflowDefinition workflow = core::WorkflowDefinition::create(
        std::move(workflow_name),
        std::move(tasks));

    // Phase 5: Semantic workflow validation (missing dependencies, cycles)
    auto semantic_diagnostics = core::WorkflowValidator::validate(workflow);
    if (!semantic_diagnostics.is_valid()) {
        return ManifestParseResult::failure(std::move(semantic_diagnostics));
    }

    return ManifestParseResult::success(std::move(workflow));
}

std::optional<ManifestParseResult> WorkflowManifestParser::parse_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parse_string(content);
}

} // namespace taskforge::manifest
