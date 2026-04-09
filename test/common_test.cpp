#include <doctest/doctest.h>

#include "common.hpp"

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

TEST_CASE("trim with tabs is not trimmed") {
    CHECK(trim("\thello\t") == "\thello\t");
}

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

TEST_CASE("extract_between with overlapping delimiter text") {
    CHECK(extract_between("aabaa", {"aa", "aa"}) == "b");
}

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
    const auto dir = std::filesystem::temp_directory_path() / "klspw_common_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "test_rw.txt";

    klspw::write_file(path, "hello world");
    const auto content = klspw::read_file(path);
    CHECK(content == "hello world");

    std::filesystem::remove(path);
    std::filesystem::remove(dir);
}

TEST_CASE("write_file creates file with binary content") {
    const auto path = std::filesystem::temp_directory_path() / "klspw_binary_test.bin";
    const std::string binary_data = {'\x00', '\x01', '\x02', '\xff'};

    klspw::write_file(path, binary_data);
    const auto content = klspw::read_file(path);
    CHECK(content == binary_data);

    std::filesystem::remove(path);
}

TEST_CASE("read_file throws on empty path") {
    CHECK_THROWS_AS(klspw::read_file(""), std::runtime_error);
}

TEST_CASE("write_file throws on empty path") {
    CHECK_THROWS_AS(klspw::write_file("", "data"), std::runtime_error);
}

TEST_CASE("read_file throws on nonexistent file") {
    CHECK_THROWS_AS(klspw::read_file("/tmp/klspw_nonexistent_file_xyz.txt"), std::runtime_error);
}

// --- require ---

TEST_CASE("require does not throw when condition is true") {
    CHECK_NOTHROW(klspw::require(true, "should not throw"));
}

TEST_CASE("require throws when condition is false") {
    CHECK_THROWS_AS(klspw::require(false, "expected failure"), std::runtime_error);
}

TEST_CASE("require formats message with args") {
    try {
        klspw::require(false, "value is {}", 42);
        FAIL("should have thrown");
    } catch (const std::runtime_error& e) {
        CHECK(std::string_view{e.what()}.contains("42"));
    }
}

TEST_CASE("require auto-converts fs::path") {
    try {
        klspw::require(false, "path: {}", std::filesystem::path{"/tmp/test"});
        FAIL("should have thrown");
    } catch (const std::runtime_error& e) {
        CHECK(std::string_view{e.what()}.contains("/tmp/test"));
    }
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
