#include <iostream>
#include <string>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

namespace {

void set_log_level(const std::string& level) {
    static const std::map<std::string, spdlog::level::level_enum> levels = {
        {"trace", spdlog::level::trace}, {"debug", spdlog::level::debug}, {"info", spdlog::level::info},
        {"warn", spdlog::level::warn},   {"error", spdlog::level::err},   {"off", spdlog::level::off},
    };
    if (const auto it = levels.find(level); it != levels.end()) {
        spdlog::set_level(it->second);
    } else {
        throw CLI::ValidationError("Invalid log level: " + level);
    }
}

} // namespace

int main(int argc, char* argv[]) try {
    CLI::App app{"klspw - Kotlin LSP workspace generator"};
    app.require_subcommand(0, 1);

    std::string log_level = "warn";
    app.add_option("--log-level", log_level, "Log level: trace, debug, info, warn, error, off")
        ->default_val("warn")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "off"}));

    auto* generate = app.add_subcommand("generate", "Generate workspace.json");
    auto* inspect = app.add_subcommand("inspect", "Print discovered modules, jars, and source roots");
    auto* validate = app.add_subcommand("validate", "Validate config and discovered paths");

    CLI11_PARSE(app, argc, argv);

    set_log_level(log_level);
    spdlog::debug("log level set to {}", log_level);

    if (generate->parsed()) {
        std::cout << "generate: not yet implemented\n";
    } else if (inspect->parsed()) {
        std::cout << "inspect: not yet implemented\n";
    } else if (validate->parsed()) {
        std::cout << "validate: not yet implemented\n";
    }

    return 0;
} catch (const std::exception& e) {
    spdlog::critical("{}", e.what());
    return 1;
} catch (...) {
    spdlog::critical("Unknown fatal error");
    return 1;
}
