#include "taskforge/cli/cli_app.hpp"
#include "taskforge/manifest/manifest_parser.hpp"

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

void CliApp::print_usage() {
    out_ << "TaskForge — Deterministic Local Workflow Orchestrator\n"
         << "Usage: taskforge <command> [options]\n\n"
         << "Available commands:\n"
         << "  validate <workflow-file>   Validate a workflow manifest without executing tasks\n"
         << "  help, --help, -h           Show this help message\n";
}

} // namespace taskforge::cli
