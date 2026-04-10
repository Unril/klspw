#include <array>
#include <iostream>
#include <ranges>
#include <set>
#include <string>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "pipeline.hpp"

namespace {

struct LogLevel {
    std::string_view name;
    spdlog::level::level_enum level;
};

constexpr std::array log_levels = {
    LogLevel{.name = "trace", .level = spdlog::level::trace},
    LogLevel{.name = "debug", .level = spdlog::level::debug},
    LogLevel{.name = "info", .level = spdlog::level::info},
    LogLevel{.name = "warn", .level = spdlog::level::warn},
    LogLevel{.name = "error", .level = spdlog::level::err},
    LogLevel{.name = "off", .level = spdlog::level::off},
};

std::set<std::string> log_level_names() {
    return log_levels | std::views::transform(&LogLevel::name) | std::ranges::to<std::set<std::string>>();
}

void set_log_level(std::string_view level) {
    const auto* it = std::ranges::find(log_levels, level, &LogLevel::name);
    klspw::require(it != log_levels.end(), "Invalid log level: {}", level);
    spdlog::set_level(it->level);
}

} // namespace

int main(int argc, char* argv[]) try {
    CLI::App app{{"klspw - Kotlin LSP workspace generator"}};
    app.require_subcommand(1);

    std::string config_path;
    std::string log_level = "warn";

    app.add_option("-c,--config", config_path, "Path to config YAML file");
    app.add_option("-l,--log-level", log_level, "Log level: trace, debug, info, warn, error, off")
        ->default_val("warn")
        ->check(CLI::IsMember(log_level_names()));

    // --- init subcommand ---
    auto* init = app.add_subcommand("init", "Generate a starter config YAML for a Gradle root");
    std::string init_root;
    std::string init_jvm_target = "21";
    init->add_option("root", init_root, "Path to Gradle root (directory containing build.gradle.kts)")->required();
    init->add_option("--jvm-target", init_jvm_target, "JVM target version")->default_val("21");

    // --- subcommands requiring --config ---
    auto* gen = app.add_subcommand("generate", "Generate workspace.json");
    auto* insp = app.add_subcommand("inspect", "Print discovered modules, jars, and source roots");
    auto* val = app.add_subcommand("validate", "Validate config and discovered paths");

    std::string gradle_output_path;
    for (auto* sub : {gen, insp}) {
        sub->add_option("--save-gradle-output", gradle_output_path, "Save raw Gradle output to a file");
    }

    CLI11_PARSE(app, argc, argv);

    set_log_level(log_level);

    if (init->parsed()) {
        auto starter = klspw::StarterConfig{init_root}.set_jvm_target(init_jvm_target).set_config_path(config_path);

        if (!starter.config_path().empty()) {
            starter.save_yaml_file();
        } else {
            std::cout << starter.to_yaml();
        }
        return 0;
    }

    // All other subcommands use --config (defaults to ./klspw.yaml).
    auto cfg = klspw::Config::load_yaml_file(config_path);

    if (val->parsed()) {
        cfg.validate();
        spdlog::info("Config valid.");
        return 0;
    }

    klspw::GradleRunner runner;
    klspw::Pipeline pipeline{std::move(cfg), std::ref(runner)};

    if (!gradle_output_path.empty()) {
        pipeline.set_gradle_output_path(gradle_output_path);
    }

    if (gen->parsed()) {
        pipeline.write_workspace();
    } else if (insp->parsed()) {
        pipeline.log_workspace();
    }

    return 0;
} catch (const std::exception& e) {
    spdlog::critical("{}", e.what());
    return 1;
} catch (...) {
    spdlog::critical("Unknown fatal error");
    return 1;
}
