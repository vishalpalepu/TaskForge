#include "taskforge/cli/cli_app.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    taskforge::cli::CliApp app(std::cout, std::cerr);
    return app.run(argc, argv);
}
