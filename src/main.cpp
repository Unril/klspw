#include <iostream>

#include <CLI/CLI.hpp>

int main(int argc, char* argv[]) {
    CLI::App app{"klspw - Kotlin LSP workspace generator"};
    app.require_subcommand(0, 1);

    auto* generate = app.add_subcommand("generate", "Generate workspace.json");
    auto* inspect = app.add_subcommand("inspect", "Print discovered modules, jars, and source roots");
    auto* validate = app.add_subcommand("validate", "Validate config and discovered paths");

    CLI11_PARSE(app, argc, argv);

    if (generate->parsed()) {
        std::cout << "generate: not yet implemented\n";
    } else if (inspect->parsed()) {
        std::cout << "inspect: not yet implemented\n";
    } else if (validate->parsed()) {
        std::cout << "validate: not yet implemented\n";
    }

    return 0;
}
