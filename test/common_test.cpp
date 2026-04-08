#include <doctest/doctest.h>

#include "common.hpp"
#include "json.hpp"
#include "yaml.hpp"

using namespace klspw;

// --- trim ---

TEST_CASE("trim removes leading and trailing whitespace") {
    CHECK(trim("  hello  ") == "hello");
    CHECK(trim("\n\nhello\n\n") == "hello");
    CHECK(trim("\r\n hello \r\n") == "hello");
    CHECK(trim("  \n\r  ") == "");
    CHECK(trim("") == "");
    CHECK(trim("no-whitespace") == "no-whitespace");
    CHECK(trim("  inner space  ") == "inner space");
}

TEST_CASE("trim preserves interior whitespace") {
    CHECK(trim("  a b  ") == "a b");
    CHECK(trim("\nline1\nline2\n") == "line1\nline2");
}

// --- join ---

TEST_CASE("join with default separator") {
    CHECK(join({"a", "b", "c"}) == "a b c");
    CHECK(join({}) == "");
    CHECK(join({"single"}) == "single");
}

TEST_CASE("join with custom separator") {
    CHECK(join({"a", "b", "c"}, ", ") == "a, b, c");
    CHECK(join({"x", "y"}, "::") == "x::y");
    CHECK(join({"only"}, "--") == "only");
}

// --- write_opt ---

TEST_CASE("write_opt omits key when nullopt") {
    json j = json::object();
    const opt_string val;
    write_opt(j, "key", val);
    CHECK_FALSE(j.contains("key"));
}

TEST_CASE("write_opt writes key when present") {
    json j = json::object();
    const opt_string val = "hello";
    write_opt(j, "key", val);
    CHECK(j["key"] == "hello");
}

// --- write_nullable ---

TEST_CASE("write_nullable writes null when nullopt") {
    json j = json::object();
    const opt_string val;
    write_nullable(j, "key", val);
    CHECK(j.contains("key"));
    CHECK(j["key"].is_null());
}

TEST_CASE("write_nullable writes value when present") {
    json j = json::object();
    const opt_string val = "hello";
    write_nullable(j, "key", val);
    CHECK(j["key"] == "hello");
}

// --- read_opt ---

TEST_CASE("read_opt reads present value") {
    const json j = {{"key", "hello"}};
    auto val = read_opt<string>(j, "key");
    REQUIRE(val.has_value());
    CHECK(val.value() == "hello");
}

TEST_CASE("read_opt returns nullopt for missing key") {
    const json j = json::object();
    auto val = read_opt<string>(j, "key");
    CHECK_FALSE(val.has_value());
}

TEST_CASE("read_opt returns nullopt for null value") {
    const json j = {{"key", nullptr}};
    auto val = read_opt<string>(j, "key");
    CHECK_FALSE(val.has_value());
}

// --- read_or ---

TEST_CASE("read_or reads present value") {
    const json j = {{"key", 42}};
    CHECK(read_or(j, "key", 0) == 42);
}

TEST_CASE("read_or returns default for missing key") {
    const json j = json::object();
    CHECK(read_or(j, "key", 99) == 99);
}

TEST_CASE("read_or works with string type") {
    const json j = {{"name", "hello"}};
    CHECK(read_or<string>(j, "name", "") == "hello");
    CHECK(read_or<string>(j, "missing", "default") == "default");
}

TEST_CASE("read_or works with vector type") {
    const json j = {{"items", {"a", "b"}}};
    auto items = read_or<strings>(j, "items", {});
    REQUIRE(items.size() == 2);
    CHECK(items[0] == "a");

    auto missing = read_or<strings>(j, "nope", {});
    CHECK(missing.empty());
}

TEST_CASE("read_or works with bool type") {
    const json j = {{"flag", true}};
    CHECK(read_or(j, "flag", false) == true);
    CHECK(read_or(j, "missing", false) == false);
}

// --- Edge cases ---

TEST_CASE("trim with only whitespace characters") {
    CHECK(trim(" ") == "");
    CHECK(trim("\n") == "");
    CHECK(trim("\r") == "");
    CHECK(trim("\r\n") == "");
}

TEST_CASE("trim with tabs is not trimmed") {
    // tabs are not in our whitespace set (space, \n, \r)
    CHECK(trim("\thello\t") == "\thello\t");
}

TEST_CASE("join with empty strings in parts") {
    CHECK(join({"", "b", ""}) == " b ");
    CHECK(join({"", ""}) == " ");
}

TEST_CASE("write_opt with non-string optional") {
    json j = json::object();
    const optional<int> val = 42;
    write_opt(j, "count", val);
    CHECK(j["count"] == 42);

    const optional<int> empty;
    write_opt(j, "missing", empty);
    CHECK_FALSE(j.contains("missing"));
}

TEST_CASE("write_nullable with non-string optional") {
    json j = json::object();
    optional<int> val;
    write_nullable(j, "count", val);
    CHECK(j["count"].is_null());

    val = 7;
    write_nullable(j, "count", val);
    CHECK(j["count"] == 7);
}

TEST_CASE("read_opt with non-string type") {
    const json j = {{"count", 42}};
    auto val = read_opt<int>(j, "count");
    REQUIRE(val.has_value());
    CHECK(val.value() == 42);
}

// --- read ---

TEST_CASE("read returns present value") {
    const json j = {{"name", "hello"}, {"count", 42}};
    CHECK(read<string>(j, "name") == "hello");
    CHECK(read<int>(j, "count") == 42);
}

TEST_CASE("read throws on missing key") {
    const json j = json::object();
    CHECK_THROWS_WITH_AS((void)read<string>(j, "missing"), "Missing required JSON field: missing", std::runtime_error);
}

TEST_CASE("read works with vector type") {
    const json j = {{"items", {"a", "b"}}};
    auto items = read<strings>(j, "items");
    REQUIRE(items.size() == 2);
    CHECK(items[0] == "a");
}

// --- write ---

TEST_CASE("write sets field unconditionally") {
    json j = json::object();
    write_field(j, "name", string{"hello"});
    write_field(j, "count", 42);
    CHECK(j["name"] == "hello");
    CHECK(j["count"] == 42);
}

// --- write_true ---

TEST_CASE("write_true writes only when true") {
    json j = json::object();
    write_true(j, "flag", true);
    write_true(j, "other", false);
    CHECK(j["flag"] == true);
    CHECK_FALSE(j.contains("other"));
}

// --- write_opt vector overload ---

TEST_CASE("write_opt omits empty vector") {
    json j = json::object();
    const strings empty;
    write_opt(j, "items", empty);
    CHECK_FALSE(j.contains("items"));
}

TEST_CASE("write_opt writes non-empty vector") {
    json j = json::object();
    const strings items = {"a", "b"};
    write_opt(j, "items", items);
    CHECK(j["items"].size() == 2);
}

// --- read_all ---

TEST_CASE("read_all reads string array") {
    const json j = {{"items", {"a", "b", "c"}}};
    auto items = read_all<string>(j, "items");
    REQUIRE(items.size() == 3);
    CHECK(items[0] == "a");
    CHECK(items[2] == "c");
}

TEST_CASE("read_all reads int array") {
    const json j = {{"nums", {1, 2, 3}}};
    auto nums = read_all<int>(j, "nums");
    REQUIRE(nums.size() == 3);
    CHECK(nums[1] == 2);
}

TEST_CASE("read_all throws on missing key") {
    const json j = json::object();
    CHECK_THROWS_WITH_AS((void)read_all<string>(j, "missing"), "Missing required JSON field: missing",
                         std::runtime_error);
}

TEST_CASE("read_all with transform") {
    const json j = {{"paths", {"/a", "/b"}}};
    auto paths = read_all<fs::path>(j, "paths", [](const json& e) { return fs::path{e.get<string>()}; });
    REQUIRE(paths.size() == 2);
    CHECK(paths[0] == fs::path{"/a"});
}

TEST_CASE("read_all returns empty for empty array") {
    const json j = {{"items", json::array()}};
    auto items = read_all<string>(j, "items");
    CHECK(items.empty());
}

// --- YAML helpers (overloads on YAML::Node) ---

TEST_CASE("yaml read returns present value") {
    auto node = YAML::Load("name: hello");
    CHECK(read<string>(node, "name") == "hello");
}

TEST_CASE("yaml read returns int") {
    auto node = YAML::Load("version: 42");
    CHECK(read<int>(node, "version") == 42);
}

TEST_CASE("yaml read throws on missing key") {
    auto node = YAML::Load("other: 1");
    CHECK_THROWS_WITH_AS((void)read<string>(node, "name"), "Config missing required field: name", std::runtime_error);
}

TEST_CASE("yaml read_or returns present value") {
    auto node = YAML::Load("name: hello");
    CHECK(read_or<string>(node, "name", "default") == "hello");
}

TEST_CASE("yaml read_or returns default for missing key") {
    auto node = YAML::Load("other: 1");
    CHECK(read_or<string>(node, "name", "fallback") == "fallback");
}

TEST_CASE("yaml read_or with bool") {
    auto node = YAML::Load("flag: true");
    CHECK(read_or(node, "flag", false) == true);
    CHECK(read_or(node, "missing", false) == false);
}

TEST_CASE("yaml read_all returns string list") {
    auto node = YAML::Load("items:\n  - a\n  - b\n  - c");
    auto items = read_all(node, "items");
    REQUIRE(items.size() == 3);
    CHECK(items[0] == "a");
    CHECK(items[2] == "c");
}

TEST_CASE("yaml read_all returns empty for missing key") {
    auto node = YAML::Load("other: 1");
    auto items = read_all(node, "items");
    CHECK(items.empty());
}

TEST_CASE("yaml read_all returns empty for empty list") {
    auto node = YAML::Load("items: []");
    auto items = read_all(node, "items");
    CHECK(items.empty());
}
