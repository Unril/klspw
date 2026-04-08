#pragma once

/// Gradle build output model and parser.
/// SourceSet, GradleProject, GradleBuildOutput represent the JSON
/// emitted by the init script between KLSPW_BEGIN/KLSPW_END delimiters.
/// GradleOutputParser extracts and deserializes that JSON.

#include <algorithm> // IWYU pragma: keep

#include "common.hpp" // IWYU pragma: keep
#include "json.hpp"

namespace klspw {

class GradleOutputParser; // constructs the model types below

// --- Gradle build output model ---

class SourceSet {
  public:
    const string& name() const { return name_; }
    const vector<fs::path>& source_roots() const { return source_roots_; }
    const vector<fs::path>& java_source_roots() const { return java_source_roots_; }
    const vector<fs::path>& resources_roots() const { return resources_roots_; }
    const vector<fs::path>& classes_dirs() const { return classes_dirs_; }
    const optional<fs::path>& resources_dir() const { return resources_dir_; }
    const vector<fs::path>& compile_classpath() const { return compile_classpath_; }
    const vector<fs::path>& runtime_classpath() const { return runtime_classpath_; }
    const string& compile_classpath_config_name() const { return compile_classpath_config_name_; }
    const string& runtime_classpath_config_name() const { return runtime_classpath_config_name_; }

    bool is_test() const { return name_.contains("test") || name_.contains("Test"); }

  private:
    friend class GradleOutputParser;

    string name_;
    vector<fs::path> source_roots_;
    vector<fs::path> java_source_roots_;
    vector<fs::path> resources_roots_;
    vector<fs::path> classes_dirs_;
    optional<fs::path> resources_dir_;
    vector<fs::path> compile_classpath_;
    vector<fs::path> runtime_classpath_;
    string compile_classpath_config_name_;
    string runtime_classpath_config_name_;
};

class GradleProject {
  public:
    const string& project_path() const { return project_path_; }
    const fs::path& project_dir() const { return project_dir_; }
    const string& kind() const { return kind_; }
    const strings& plugins() const { return plugins_; }
    const vector<SourceSet>& source_sets() const { return source_sets_; }
    const opt_string& skip_reason() const { return skip_reason_; }

    bool is_skipped() const { return skip_reason_.has_value(); }
    string module_name() const { return project_dir_.filename().string(); }

  private:
    friend class GradleOutputParser;

    string project_path_;
    fs::path project_dir_;
    string kind_;
    strings plugins_;
    vector<SourceSet> source_sets_;
    opt_string skip_reason_;
};

class GradleBuildOutput {
  public:
    const fs::path& root_project() const { return root_project_; }
    const vector<GradleProject>& projects() const { return projects_; }

    size_t active_project_count() const {
        return static_cast<size_t>(count_if(projects_, [](const auto& p) { return !p.is_skipped(); }));
    }

  private:
    friend class GradleOutputParser;

    fs::path root_project_;
    vector<GradleProject> projects_;
};

// --- Parser ---

class GradleOutputParser {
  public:
    static constexpr string_view begin_delimiter = "KLSPW_BEGIN";
    static constexpr string_view end_delimiter = "KLSPW_END";

    static string extract_json(string_view raw_output) {
        const auto begin_pos = raw_output.find(begin_delimiter);
        if (begin_pos == string_view::npos) {
            throw runtime_error("KLSPW_BEGIN delimiter not found in Gradle output");
        }
        const auto json_start = begin_pos + begin_delimiter.size();
        const auto end_pos = raw_output.find(end_delimiter, json_start);
        if (end_pos == string_view::npos) {
            throw runtime_error("KLSPW_END delimiter not found in Gradle output");
        }
        auto block = raw_output.substr(json_start, end_pos - json_start);
        block = trim(block);
        return string{block};
    }

    static GradleBuildOutput parse(string_view json_str) {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(json_str);
        } catch (const nlohmann::json::parse_error& e) {
            throw runtime_error(format("Failed to parse Gradle JSON: {}", e.what()));
        }

        GradleBuildOutput output;
        output.root_project_ = fs::path{read<string>(j, "rootProject")};
        output.projects_ = read_all<GradleProject>(j, "projects", [](const json& pj) {
            return parse_project(pj);
        });
        return output;
    }

  private:
    static GradleProject parse_project(const nlohmann::json& j) {
        GradleProject proj;
        proj.project_path_ = read<string>(j, "projectPath");
        proj.project_dir_ = fs::path{read<string>(j, "projectDir")};
        proj.kind_ = read<string>(j, "kind");
        proj.plugins_ = read_all<string>(j, "plugins");
        for (const auto& ss_json : j.at("sourceSets")) {
            proj.source_sets_.push_back(parse_source_set(ss_json));
        }
        proj.skip_reason_ = read_opt<string>(j, "skipReason");
        return proj;
    }

    static SourceSet parse_source_set(const nlohmann::json& j) {
        SourceSet ss;
        ss.name_ = read<string>(j, "name");
        ss.source_roots_ = read_all<fs::path>(j, "sourceRoots", to_path);
        ss.java_source_roots_ = read_all<fs::path>(j, "javaSourceRoots", to_path);
        ss.resources_roots_ = read_all<fs::path>(j, "resourcesRoots", to_path);
        ss.classes_dirs_ = read_all<fs::path>(j, "classesDirs", to_path);
        ss.resources_dir_ = read_opt<string>(j, "resourcesDir").transform([](const string& s) { return fs::path{s}; });
        ss.compile_classpath_ = read_all<fs::path>(j, "compileClasspath", to_path);
        ss.runtime_classpath_ = read_all<fs::path>(j, "runtimeClasspath", to_path);
        ss.compile_classpath_config_name_ = read_or<string>(j, "compileClasspathConfigurationName", "");
        ss.runtime_classpath_config_name_ = read_or<string>(j, "runtimeClasspathConfigurationName", "");
        return ss;
    }
};

} // namespace klspw
