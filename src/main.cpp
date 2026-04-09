#include <iostream>
#include <string>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "pipeline.hpp"

namespace {

void set_log_level(const std::string& level) {
    static const std::unordered_map<std::string, spdlog::level::level_enum> levels = {
        {"trace", spdlog::level::trace}, {"debug", spdlog::level::debug}, {"info", spdlog::level::info},
        {"warn", spdlog::level::warn},   {"error", spdlog::level::err},   {"off", spdlog::level::off},
    };
    const auto it = levels.find(level);
    klspw::require(it != levels.end(), "Invalid log level: {}", level);
    spdlog::set_level(it->second);
}

} // namespace

int main(int argc, char* argv[]) try {
    CLI::App app{"klspw - Kotlin LSP workspace generator"};
    app.require_subcommand(0, 1);

    std::string config_path;
    std::string log_level = "warn";

    app.add_option("-c,--config", config_path, "Path to config YAML file")->required();
    app.add_option("--log-level", log_level, "Log level: trace, debug, info, warn, error, off")
        ->default_val("warn")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "off"}));

    auto* gen = app.add_subcommand("generate", "Generate workspace.json");
    auto* insp = app.add_subcommand("inspect", "Print discovered modules, jars, and source roots");
    auto* val = app.add_subcommand("validate", "Validate config and discovered paths");

    CLI11_PARSE(app, argc, argv);

    set_log_level(log_level);

    auto cfg = klspw::Config::from_yaml(config_path);

    if (val->parsed()) {
        cfg.validate();
        spdlog::info("Config valid.");
        return 0;
    }

    const klspw::GradleRunner runner;
    const klspw::Pipeline pipeline{std::move(cfg), runner};

    if (gen->parsed()) {
        pipeline.write_workspace();
    } else if (insp->parsed()) {
        pipeline.log_workspace();
    } else {
        std::cout << app.help();
    }

    return 0;
} catch (const std::exception& e) {
    spdlog::critical("{}", e.what());
    return 1;
} catch (...) {
    spdlog::critical("Unknown fatal error");
    return 1;
}
