#include <doctest/doctest.h>

#include "common.hpp"
#include "files.hpp"
#include "ranges.hpp"
#include "strings.hpp"
#include "test_common.hpp"

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

// --- Edge cases ---

TEST_CASE("trim with only whitespace characters") {
    CHECK(trim(" ") == "");
    CHECK(trim("\n") == "");
    CHECK(trim("\r") == "");
    CHECK(trim("\r\n") == "");
}

TEST_CASE("trim with tabs is not trimmed") { CHECK(trim("\thello\t") == "\thello\t"); }

TEST_CASE("join with empty strings in parts") {
    CHECK(join({"", "b", ""}) == " b ");
    CHECK(join({"", ""}) == " ");
}

// --- extract_between ---

TEST_CASE("extract_between finds content between delimiters") {
    CHECK(extract_between("<<hello>>", {"<<", ">>"}) == "hello");
    CHECK(extract_between("noise BEGIN data END tail", {"BEGIN ", " END"}) == "data");
}

TEST_CASE("extract_between trims whitespace from result") {
    CHECK(extract_between("<<  hello  >>", {"<<", ">>"}) == "hello");
    CHECK(extract_between("<<\n  content\n>>", {"<<", ">>"}) == "content");
}

TEST_CASE("extract_between returns nullopt when open delimiter missing") {
    CHECK_FALSE(extract_between("no open >> here", {"<<", ">>"}).has_value());
}

TEST_CASE("extract_between returns nullopt when close delimiter missing") {
    CHECK_FALSE(extract_between("<< no close here", {"<<", ">>"}).has_value());
}

TEST_CASE("extract_between returns nullopt on empty input") {
    CHECK_FALSE(extract_between("", {"<<", ">>"}).has_value());
}

TEST_CASE("extract_between returns empty string when delimiters are adjacent") {
    CHECK(extract_between("<<>>", {"<<", ">>"}) == "");
}

TEST_CASE("extract_between uses first occurrence of open delimiter") {
    CHECK(extract_between("<< first >> << second >>", {"<<", ">>"}) == "first");
}

TEST_CASE("extract_between works with single-char delimiters") {
    CHECK(extract_between("[content]", {"[", "]"}) == "content");
}

TEST_CASE("extract_between with multiline content") {
    const auto result = extract_between("---\nBEGIN\nline1\nline2\nEND\n---", {"BEGIN", "END"});
    CHECK(result == "line1\nline2");
}

TEST_CASE("extract_between when open appears but close is before open") {
    CHECK_FALSE(extract_between("END stuff BEGIN", {"BEGIN", "END"}).has_value());
}

TEST_CASE("extract_between with same open and close delimiter") {
    CHECK(extract_between("| hello |", {"|", "|"}) == "hello");
}

TEST_CASE("extract_between with overlapping delimiter text") { CHECK(extract_between("aabaa", {"aa", "aa"}) == "b"); }

// --- unique_by ---

TEST_CASE("unique_by deduplicates by identity") {
    const std::vector<std::string> input = {"a", "b", "a", "c", "b"};
    CHECK((input | klspw::unique_by()) == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("unique_by preserves first occurrence order") {
    const std::vector<int> input = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3};
    CHECK((input | klspw::unique_by()) == std::vector<int>{3, 1, 4, 5, 9, 2, 6});
}

TEST_CASE("unique_by with projection") {
    struct item {
        std::string key;
        int value;
    };
    const std::vector<item> input = {{"a", 1}, {"b", 2}, {"a", 3}, {"c", 4}};
    const auto result = input | klspw::unique_by(&item::key);
    REQUIRE(result.size() == 3);
    CHECK(result[0].value == 1); // first "a" kept
    CHECK(result[1].value == 2);
    CHECK(result[2].value == 4);
}

TEST_CASE("unique_by with lambda projection") {
    const std::vector<std::string> input = {"hello", "HELLO", "world", "World"};
    const auto result = input | klspw::unique_by([](const auto& s) {
        std::string lower(s.size(), '\0');
        std::ranges::transform(s, lower.begin(), ::tolower);
        return lower;
    });
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "hello");
    CHECK(result[1] == "world");
}

TEST_CASE("unique_by on empty range") {
    const std::vector<int> input;
    CHECK((input | klspw::unique_by()).empty());
}

TEST_CASE("unique_by on range with no duplicates") {
    const std::vector<int> input = {1, 2, 3};
    CHECK((input | klspw::unique_by()) == input);
}

// --- read_file / write_file ---

TEST_CASE("write_file and read_file round-trip") {
    const TempDir dir;
    const auto path = dir.path / "test_rw.txt";

    klspw::write_file(path, "hello world");
    CHECK(klspw::read_file(path) == "hello world");
}

TEST_CASE("write_file creates file with binary content") {
    const TempDir dir;
    const auto path = dir.path / "binary_test.bin";
    const std::string binary_data = {'\x00', '\x01', '\x02', '\xff'};

    klspw::write_file(path, binary_data);
    CHECK(klspw::read_file(path) == binary_data);
}

TEST_CASE("read_file throws on empty path") {
    CHECK_THROWS_WITH_AS(klspw::read_file(""), doctest::Contains("empty path"), std::runtime_error);
}

TEST_CASE("write_file throws on empty path") {
    CHECK_THROWS_WITH_AS(klspw::write_file("", "data"), doctest::Contains("empty path"), std::runtime_error);
}

TEST_CASE("read_file throws on nonexistent file") {
    CHECK_THROWS_WITH_AS(klspw::read_file("/tmp/klspw_nonexistent_file_xyz.txt"),
        doctest::Contains("Cannot determine file size"),
        std::runtime_error);
}

// --- require ---

TEST_CASE("require does not throw when condition is true") { CHECK_NOTHROW(klspw::require(true, "should not throw")); }

TEST_CASE("require throws when condition is false") {
    CHECK_THROWS_AS(klspw::require(false, "expected failure"), std::runtime_error);
}

TEST_CASE("require formats message with args") {
    CHECK_THROWS_WITH_AS(klspw::require(false, "value is {}", 42), doctest::Contains("42"), std::runtime_error);
}

TEST_CASE("require auto-converts fs::path") {
    CHECK_THROWS_WITH_AS(klspw::require(false, "path: {}", std::filesystem::path{"/tmp/test"}),
        doctest::Contains("/tmp/test"),
        std::runtime_error);
}

TEST_CASE("require evaluates callable lazily") {
    int call_count = 0;
    auto lazy = [&] {
        ++call_count;
        return "lazy value";
    };

    klspw::require(true, "msg: {}", lazy);
    CHECK(call_count == 0); // not called on success

    CHECK_THROWS_AS(klspw::require(false, "msg: {}", lazy), std::runtime_error);
    CHECK(call_count == 1); // called on failure
}

// --- find_entry / find_dir / find_file ---

TEST_CASE("find_dir finds directory matching suffix") {
    const TempDir dir;
    fs::create_directories(dir.path / "a" / "b" / "target-dir");

    const auto result = klspw::find_dir(dir.path, "target-dir");
    REQUIRE(result.has_value());
    CHECK(result->string().ends_with("target-dir"));
}

TEST_CASE("find_dir matches multi-component suffix") {
    const TempDir dir;
    fs::create_directories(dir.path / "src" / "main" / "java");

    const auto result = klspw::find_dir(dir.path, "/main/java");
    REQUIRE(result.has_value());
    CHECK(result->string().ends_with("/main/java"));
}

TEST_CASE("find_dir returns nullopt when no match") {
    const TempDir dir;
    fs::create_directories(dir.path / "empty");

    CHECK_FALSE(klspw::find_dir(dir.path, "nonexistent").has_value());
}

TEST_CASE("find_file finds regular file matching suffix") {
    const TempDir dir;
    fs::create_directories(dir.path / "nested");
    klspw::write_file(dir.path / "nested" / "lib-1.0-sources.jar", "");

    const auto result = klspw::find_file(dir.path, "-sources.jar");
    REQUIRE(result.has_value());
    CHECK(result->string().ends_with("-sources.jar"));
}

TEST_CASE("find_file ignores directories with matching name") {
    const TempDir dir;
    fs::create_directories(dir.path / "fake-sources.jar"); // directory, not file

    CHECK_FALSE(klspw::find_file(dir.path, "-sources.jar").has_value());
}

TEST_CASE("find_dir ignores files with matching name") {
    const TempDir dir;
    klspw::write_file(dir.path / "target-dir", ""); // file, not directory

    CHECK_FALSE(klspw::find_dir(dir.path, "target-dir").has_value());
}

// --- strip_prefixes ---

TEST_CASE("strip_prefixes strips at first matching marker") {
    constexpr std::array<std::string_view, 2> markers = {"/caches/", "/packages/"};
    const auto [suffix, prefix] = klspw::strip_prefixes("/Users/me/.gradle/caches/modules/lib.jar", markers);
    CHECK(suffix == "modules/lib.jar");
    CHECK(prefix == "/Users/me/.gradle/caches/");
}

TEST_CASE("strip_prefixes matches packages marker") {
    constexpr std::array<std::string_view, 2> markers = {"/caches/", "/packages/"};
    const auto [suffix, prefix] = klspw::strip_prefixes("/vol/pkg-cache/packages/Foo/Foo-1.0/lib.jar", markers);
    CHECK(suffix == "Foo/Foo-1.0/lib.jar");
    CHECK(prefix == "/vol/pkg-cache/packages/");
}

TEST_CASE("strip_prefixes returns full path when no marker matches") {
    constexpr std::array<std::string_view, 2> markers = {"/caches/", "/packages/"};
    const auto [suffix, prefix] = klspw::strip_prefixes("/home/user/project/build/lib.jar", markers);
    CHECK(suffix == "/home/user/project/build/lib.jar");
    CHECK(prefix.empty());
}

TEST_CASE("strip_prefixes with empty markers returns full path") {
    constexpr std::array<std::string_view, 0> markers = {};
    const auto [suffix, prefix] = klspw::strip_prefixes("/some/path", markers);
    CHECK(suffix == "/some/path");
    CHECK(prefix.empty());
}

TEST_CASE("strip_prefixes prefers first matching marker") {
    constexpr std::array<std::string_view, 2> markers = {"/a/", "/b/"};
    const auto [suffix, prefix] = klspw::strip_prefixes("/root/a/b/file", markers);
    CHECK(suffix == "b/file");
    CHECK(prefix == "/root/a/");
}

TEST_CASE("strip_prefixes with marker at start") {
    constexpr std::array<std::string_view, 1> markers = {"/caches/"};
    const auto [suffix, prefix] = klspw::strip_prefixes("/caches/lib.jar", markers);
    CHECK(suffix == "lib.jar");
    CHECK(prefix == "/caches/");
}
