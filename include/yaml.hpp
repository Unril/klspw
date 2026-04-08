#pragma once

/// YAML read helpers for yaml-cpp.
/// Names match json.hpp (read, read_or, read_all) -- overloads resolve on YAML::Node vs json.

#include <yaml-cpp/yaml.h>

#include "common.hpp"

namespace klspw {

/// Reads a required scalar field. Throws if missing.
template <typename T> T read(const YAML::Node& node, const string& key) {
    if (!node[key]) {
        throw runtime_error(format("Config missing required field: {}", key));
    }
    return node[key].as<T>();
}

/// Reads an optional scalar field. Returns default if missing.
template <typename T> T read_or(const YAML::Node& node, const string& key, const T& def) {
    return node[key] ? node[key].as<T>() : def;
}

/// Reads an optional string list. Returns empty if missing.
inline strings read_all(const YAML::Node& node, const string& key) {
    strings result;
    if (node[key]) {
        for (const auto& item : node[key]) {
            result.push_back(item.as<string>());
        }
    }
    return result;
}

}
