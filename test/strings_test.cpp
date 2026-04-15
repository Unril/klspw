#include "strings.hpp"

#include <array>

#include <doctest/doctest.h>

#include "ranges.hpp"

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

TEST_CASE("trim with only whitespace characters") {
  CHECK(trim(" ") == "");
  CHECK(trim("\n") == "");
  CHECK(trim("\r") == "");
  CHECK(trim("\r\n") == "");
}

TEST_CASE("trim with tabs is not trimmed") { CHECK(trim("\thello\t") == "\thello\t"); }

// --- join_to_string ---

TEST_CASE("join_to_string with default separator") {
  CHECK((std::array{"a"sv, "b"sv, "c"sv} | join_to_string()) == "a b c");
  CHECK((std::array<string_view, 0>{} | join_to_string()) == "");
  CHECK((std::array{"single"sv} | join_to_string()) == "single");
}

TEST_CASE("join_to_string with custom separator") {
  CHECK((std::array{"a"sv, "b"sv, "c"sv} | join_to_string(", ")) == "a, b, c");
  CHECK((std::array{"x"sv, "y"sv} | join_to_string("::")) == "x::y");
  CHECK((std::array{"only"sv} | join_to_string("--")) == "only");
}

TEST_CASE("join_to_string with empty strings in parts") {
  CHECK((std::array{""sv, "b"sv, ""sv} | join_to_string()) == " b ");
  CHECK((std::array{""sv, ""sv} | join_to_string()) == " ");
}

// --- extract_between ---

TEST_CASE("extract_between finds content between delimiters") {
  CHECK(extract_between("<<hello>>", {.open = "<<", .close = ">>"}) == "hello");
  CHECK(extract_between("noise BEGIN data END tail", {.open = "BEGIN ", .close = " END"}) == "data");
}

TEST_CASE("extract_between trims whitespace from result") {
  CHECK(extract_between("<<  hello  >>", {.open = "<<", .close = ">>"}) == "hello");
  CHECK(extract_between("<<\n  content\n>>", {.open = "<<", .close = ">>"}) == "content");
}

TEST_CASE("extract_between returns nullopt when open delimiter missing") {
  CHECK_FALSE(extract_between("no open >> here", {.open = "<<", .close = ">>"}).has_value());
}

TEST_CASE("extract_between returns nullopt when close delimiter missing") {
  CHECK_FALSE(extract_between("<< no close here", {.open = "<<", .close = ">>"}).has_value());
}

TEST_CASE("extract_between returns nullopt on empty input") {
  CHECK_FALSE(extract_between("", {.open = "<<", .close = ">>"}).has_value());
}

TEST_CASE("extract_between returns empty string when delimiters are adjacent") {
  CHECK(extract_between("<<>>", {.open = "<<", .close = ">>"}) == "");
}

TEST_CASE("extract_between uses first occurrence of open delimiter") {
  CHECK(extract_between("<< first >> << second >>", {.open = "<<", .close = ">>"}) == "first");
}

TEST_CASE("extract_between works with single-char delimiters") {
  CHECK(extract_between("[content]", {.open = "[", .close = "]"}) == "content");
}

TEST_CASE("extract_between with multiline content") {
  const auto result = extract_between("---\nBEGIN\nline1\nline2\nEND\n---", {.open = "BEGIN", .close = "END"});
  CHECK(result == "line1\nline2");
}

TEST_CASE("extract_between when open appears but close is before open") {
  CHECK_FALSE(extract_between("END stuff BEGIN", {.open = "BEGIN", .close = "END"}).has_value());
}

TEST_CASE("extract_between with same open and close delimiter") {
  CHECK(extract_between("| hello |", {.open = "|", .close = "|"}) == "hello");
}

TEST_CASE("extract_between with overlapping delimiter text") {
  CHECK(extract_between("aabaa", {.open = "aa", .close = "aa"}) == "b");
}

// --- Delimiters::validate ---

TEST_CASE("Delimiters validate throws on empty open delimiter") {
  CHECK_THROWS_AS(extract_between("input", {.open = "", .close = ">>"}), std::runtime_error);
}

TEST_CASE("Delimiters validate throws on empty close delimiter") {
  CHECK_THROWS_AS(extract_between("input", {.open = "<<", .close = ""}), std::runtime_error);
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

// --- split_words ---

TEST_CASE("split_words splits on spaces") {
  CHECK(klspw::split_words("hello world") == klspw::strings{"hello", "world"});
}

TEST_CASE("split_words single word") { CHECK(klspw::split_words("gradlew") == klspw::strings{"gradlew"}); }

TEST_CASE("split_words trims leading and trailing spaces") {
  CHECK(klspw::split_words("  hello  world  ") == klspw::strings{"hello", "world"});
}

TEST_CASE("split_words skips multiple consecutive spaces") {
  CHECK(klspw::split_words("a   b   c") == klspw::strings{"a", "b", "c"});
}

TEST_CASE("split_words empty string returns empty") { CHECK(klspw::split_words("").empty()); }

TEST_CASE("split_words whitespace-only returns empty") { CHECK(klspw::split_words("   ").empty()); }

TEST_CASE("split_words preserves flags") {
  CHECK(klspw::split_words("my_build --quiet --no-daemon") == klspw::strings{"my_build", "--quiet", "--no-daemon"});
}

// --- MavenCoords::parse ---

TEST_CASE("MavenCoords::parse parses valid coordinates") {
  const auto coords = klspw::MavenCoords::parse("org.jetbrains:annotations:24.0.0");
  REQUIRE(coords.has_value());
  CHECK(coords->group == "org.jetbrains");
  CHECK(coords->module == "annotations");
  CHECK(coords->version == "24.0.0");
}

TEST_CASE("MavenCoords::parse returns nullopt for no colons") {
  CHECK_FALSE(klspw::MavenCoords::parse("no-colons-here").has_value());
}

TEST_CASE("MavenCoords::parse returns nullopt for single colon") {
  CHECK_FALSE(klspw::MavenCoords::parse("group:module").has_value());
}

TEST_CASE("MavenCoords::parse handles extra colons after version") {
  const auto coords = klspw::MavenCoords::parse("g:m:1.0:classifier");
  REQUIRE(coords.has_value());
  CHECK(coords->group == "g");
  CHECK(coords->module == "m");
  CHECK(coords->version == "1.0:classifier");
}

TEST_CASE("MavenCoords::parse handles empty components") {
  const auto coords = klspw::MavenCoords::parse("::v");
  REQUIRE(coords.has_value());
  CHECK(coords->group.empty());
  CHECK(coords->module.empty());
  CHECK(coords->version == "v");
}
