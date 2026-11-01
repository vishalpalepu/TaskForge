#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace taskforge::cli {

/**
 * @brief Standardized exit codes returned by TaskForge CLI commands.
 */
enum class ExitCode : int {
    Success = 0,          ///< Command succeeded (e.g. manifest is valid, all tasks succeeded).
    InvalidUsage = 1,     ///< Missing/invalid flags or unrecognized command.
    FileNotFound = 2,     ///< Manifest file does not exist or cannot be read.
    ValidationFailed = 3, ///< Manifest failed syntax, schema, or semantic validation.
    ExecutionFailed = 4   ///< Workflow executed but at least one task failed or was blocked.
};

/**
 * @brief Top-level command dispatcher for the TaskForge command-line interface.
 *
 * Implements Single Responsibility Principle:
 * - Parses CLI arguments.
 * - Routes to subcommand handlers.
 * - Formats diagnostics to standard error and summaries to standard output.
 * - Completely decoupled from OS process execution during 'validate'.
 */
class CliApp {
public:
    explicit CliApp(std::ostream& out, std::ostream& err) noexcept;

    /// @brief Main entry point from C main(argc, argv).
    [[nodiscard]] int run(int argc, const char* const argv[]);

    /// @brief Overload accepting a vector of strings for hermetic unit testing.
    [[nodiscard]] int run(const std::vector<std::string>& args);

private:
    int handle_validate(const std::vector<std::string>& sub_args);
    int handle_run(const std::vector<std::string>& sub_args);
    void print_usage();

    std::ostream& out_;
    std::ostream& err_;
};

} // namespace taskforge::cli
