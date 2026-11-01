#include "taskforge/cli/cli_app.hpp"
#include "taskforge/manifest/manifest_parser.hpp"
#include "taskforge/scheduler/workflow_scheduler.hpp"

#if defined(_WIN32)
#include "taskforge/platform/win32/windows_process_executor.hpp"
#endif

#include <iostream>

namespace taskforge::cli {

CliApp::CliApp(std::ostream& out, std::ostream& err) noexcept
    : out_(out), err_(err) {}

int CliApp::run(int argc, const char* const argv[]) {
    std::vector<std::string> args;
    args.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr) {
            args.emplace_back(argv[i]);
        }
    }
    return run(args);
}

int CliApp::run(const std::vector<std::string>& args) {
    if (args.empty()) {
        err_ << "Error: No command specified.\n";
        print_usage();
        return static_cast<int>(ExitCode::InvalidUsage);
    }

    const std::string& cmd = args[0];
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        print_usage();
        return static_cast<int>(ExitCode::Success);
    }

    if (cmd == "validate") {
        std::vector<std::string> sub_args(args.begin() + 1, args.end());
        return handle_validate(sub_args);
    }

    if (cmd == "run") {
        std::vector<std::string> sub_args(args.begin() + 1, args.end());
        return handle_run(sub_args);
    }

    err_ << "Error: Unrecognized command '" << cmd << "'.\n";
    print_usage();
    return static_cast<int>(ExitCode::InvalidUsage);
}

int CliApp::handle_validate(const std::vector<std::string>& sub_args) {
    if (sub_args.size() != 1) {
        err_ << "Error: 'validate' requires exactly one manifest file argument.\n";
        err_ << "Usage: taskforge validate <workflow-file>\n";
        return static_cast<int>(ExitCode::InvalidUsage);
    }

    const std::string& file_arg = sub_args[0];
    std::filesystem::path manifest_path = manifest::utf8_to_path(file_arg);

    auto result_opt = manifest::WorkflowManifestParser::parse_file(manifest_path);
    if (!result_opt.has_value()) {
        err_ << "Error: Cannot open manifest file '" << file_arg << "': File not found or unreadable.\n";
        return static_cast<int>(ExitCode::FileNotFound);
    }

    const auto& parse_result = *result_opt;
    if (!parse_result.is_valid()) {
        err_ << parse_result.diagnostics().format_summary();
        return static_cast<int>(ExitCode::ValidationFailed);
    }

    const auto& workflow = parse_result.workflow();
    out_ << "Workflow '" << workflow.name() << "' is valid ("
         << workflow.task_count() << " task" << (workflow.task_count() == 1 ? "" : "s") << ").\n";
    return static_cast<int>(ExitCode::Success);
}

int CliApp::handle_run(const std::vector<std::string>& sub_args) {
    if (sub_args.size() != 1) {
        err_ << "Error: 'run' requires exactly one manifest file argument.\n";
        err_ << "Usage: taskforge run <workflow-file>\n";
        return static_cast<int>(ExitCode::InvalidUsage);
    }

    const std::string& file_arg = sub_args[0];
    std::filesystem::path manifest_path = manifest::utf8_to_path(file_arg);

    auto result_opt = manifest::WorkflowManifestParser::parse_file(manifest_path);
    if (!result_opt.has_value()) {
        err_ << "Error: Cannot open manifest file '" << file_arg << "': File not found or unreadable.\n";
        return static_cast<int>(ExitCode::FileNotFound);
    }

    const auto& parse_result = *result_opt;
    if (!parse_result.is_valid()) {
        err_ << parse_result.diagnostics().format_summary();
        return static_cast<int>(ExitCode::ValidationFailed);
    }

    const auto& workflow = parse_result.workflow();

#if defined(_WIN32)
    platform::win32::WindowsProcessExecutor executor;
    scheduler::WorkflowScheduler scheduler(executor);
#else
    err_ << "Error: Process execution is only supported on Windows in this build.\n";
    return static_cast<int>(ExitCode::ExecutionFailed);
#endif

    out_ << "Executing workflow '" << workflow.name() << "' ("
         << workflow.task_count() << " task" << (workflow.task_count() == 1 ? "" : "s") << ")...\n";

    auto run_result = scheduler.run(workflow);
    if (!run_result.is_valid()) {
        err_ << run_result.validation_result().format_summary();
        return static_cast<int>(ExitCode::ValidationFailed);
    }

    const auto& execution = run_result.execution();

    for (const auto& task : workflow.tasks()) {
        const auto* res = execution.task_result(task.id());
        if (!res) {
            continue;
        }

        out_ << "[" << res->final_state() << "] " << task.id().value();
        if (res->is_success()) {
            out_ << " (completed in " << res->duration().count() << "ms)\n";
        } else if (res->is_failed()) {
            out_ << " (failed: " << res->failure_category();
            if (res->exit_code().has_value()) {
                out_ << ", exit code " << *res->exit_code();
            }
            if (res->os_error_code().has_value()) {
                out_ << ", OS error " << *res->os_error_code();
            }
            out_ << ")\n";
            if (res->error_message().has_value() && !res->error_message()->empty()) {
                err_ << "  Error [" << task.id().value() << "]: " << *res->error_message() << "\n";
            }
            if (!res->standard_error().empty()) {
                err_ << "  Stderr [" << task.id().value() << "]:\n" << res->standard_error();
                if (res->standard_error().back() != '\n') {
                    err_ << "\n";
                }
            }
        } else if (res->is_blocked()) {
            out_ << " (blocked by failed prerequisite)\n";
        } else {
            out_ << "\n";
        }
    }

    out_ << "Workflow execution finished: "
         << execution.count_with_state(core::TaskState::Succeeded) << " succeeded, "
         << execution.count_with_state(core::TaskState::Failed) << " failed, "
         << execution.count_with_state(core::TaskState::Blocked) << " blocked.\n";

    if (run_result.is_successful()) {
        return static_cast<int>(ExitCode::Success);
    } else {
        return static_cast<int>(ExitCode::ExecutionFailed);
    }
}

void CliApp::print_usage() {
    out_ << "TaskForge — Deterministic Local Workflow Orchestrator\n"
         << "Usage: taskforge <command> [options]\n\n"
         << "Available commands:\n"
         << "  run <workflow-file>        Execute tasks in a workflow manifest sequentially\n"
         << "  validate <workflow-file>   Validate a workflow manifest without executing tasks\n"
         << "  help, --help, -h           Show this help message\n";
}

} // namespace taskforge::cli
