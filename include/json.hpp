#pragma once

/// JSON read/write helpers for nlohmann/json.

#include "common.hpp"

namespace klspw {

// --- Write helpers ---

/// Writes a required field unconditionally.
template <typename T> void write_field(json& j, const string& key, const T& val) {
    j[key] = val;
}

/// Writes a bool field only when true; omits when false.
inline void write_true(json& j, const string& key, bool val) {
    if (val) {
        j[key] = true;
    }
}

/// Writes an optional field only when present; omits the key entirely when nullopt.
template <typename T> void write_opt(json& j, const string& key, const optional<T>& val) {
    if (val) {
        j[key] = *val;
    }
}

/// Writes a vector field only when non-empty; omits the key entirely when empty.
template <typename T> void write_opt(json& j, const string& key, const vector<T>& val) {
    if (!val.empty()) {
        j[key] = val;
    }
}

/// Writes an optional field as explicit JSON null when absent.
/// Use for fields that kotlin-lsp expects to always be present (e.g., compilerArguments).
template <typename T> void write_nullable(json& j, const string& key, const optional<T>& val) {
    j[key] = val ? json(*val) : json(nullptr);
}

// --- Read helpers ---

/// Reads an optional field; missing or null → nullopt.
template <typename T> optional<T> read_opt(const json& j, const string& key) {
    if (j.contains(key) && !j[key].is_null()) {
        return j[key].get<T>();
    }
    return nullopt;
}

/// Reads a required field; throws with context if missing.
template <typename T> T read(const json& j, const string& key) {
    if (!j.contains(key)) {
        throw runtime_error(format("Missing required JSON field: {}", key));
    }
    return j[key].get<T>();
}

/// Reads a field with a default; missing → default.
template <typename T> T read_or(const json& j, const string& key, const T& def) {
    return j.contains(key) ? j[key].get<T>() : def;
}

/// Reads a required JSON array, converting each element via a transform function.
template <typename T, typename Fn> vector<T> read_all(const json& j, const string& key, Fn&& transform) {
    vector<T> result;
    for (const auto& elem : read<json>(j, key)) {
        result.emplace_back(transform(elem));
    }
    return result;
}

/// Reads a required JSON array, converting each element to T.
template <typename T> vector<T> read_all(const json& j, const string& key) {
    return read_all<T>(j, key, std::identity{});
}

/// Converts a JSON string element to fs::path. For use with read_all.
inline constexpr auto to_path = [](const json& e) { return fs::path{e.get<string>()}; };

} // namespace klspw
