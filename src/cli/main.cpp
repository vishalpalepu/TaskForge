#include "taskforge/cli/cli_app.hpp"

#include <iostream>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace {

/**
 * @brief Retrieves native Windows command-line arguments converted directly to UTF-8.
 *
 * Avoids legacy Windows ANSI code page (CP_ACP) corruption when filenames or parameters
 * contain non-ASCII Unicode characters (e.g. Cyrillic, Japanese, or German umlauts).
 */
std::vector<std::string> get_windows_utf8_command_line() {
    int argc = 0;
    LPWSTR* argv_w = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv_w) {
        return {};
    }

    std::vector<std::string> args;
    args.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);

    // Skip argv[0] (executable path) to pass subcommand and flags to app.run(args)
    for (int i = 1; i < argc; ++i) {
        int len = ::WideCharToMultiByte(
            CP_UTF8, 0, argv_w[i], -1, nullptr, 0, nullptr, nullptr);
        if (len > 1) { // includes null terminator
            std::string arg(static_cast<size_t>(len - 1), '\0');
            ::WideCharToMultiByte(
                CP_UTF8, 0, argv_w[i], -1, arg.data(), len, nullptr, nullptr);
            args.push_back(std::move(arg));
        } else {
            args.push_back("");
        }
    }

    ::LocalFree(argv_w);
    return args;
}

} // namespace
#endif

int main(int argc, char* argv[]) {
    taskforge::cli::CliApp app(std::cout, std::cerr);
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    return app.run(get_windows_utf8_command_line());
#else
    return app.run(argc, argv);
#endif
}
