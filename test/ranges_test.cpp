#include "ranges.hpp"

#include <doctest/doctest.h>

using namespace klspw;

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

  const std::vector<item> input = {
      {.key = "a", .value = 1}, {.key = "b", .value = 2}, {.key = "a", .value = 3}, {.key = "c", .value = 4}};
  const auto result = input | klspw::unique_by(&item::key);
  REQUIRE(result.size() == 3);
  CHECK(result[0].value == 1);  // first "a" kept
  CHECK(result[1].value == 2);
  CHECK(result[2].value == 4);
}

TEST_CASE("unique_by with lambda projection") {
  const auto to_lower = [](const auto& s) {
    std::string lower(s.size(), '\0');
    std::ranges::transform(s, lower.begin(), ::tolower);
    return lower;
  };
  const std::vector<std::string> input = {"hello", "HELLO", "world", "World"};
  const auto result = input | klspw::unique_by(to_lower);
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

// --- not_in ---

TEST_CASE("not_in excludes elements present in the set") {
  const klspw::string_set excluded = {"b", "d"};
  const klspw::strings input = {"a", "b", "c", "d", "e"};
  const auto result = input | klspw::not_in(excluded) | klspw::to_vector();
  CHECK(result == klspw::strings{"a", "c", "e"});
}

TEST_CASE("not_in with empty exclusion set keeps all elements") {
  const klspw::string_set excluded;
  const klspw::strings input = {"a", "b", "c"};
  const auto result = input | klspw::not_in(excluded) | klspw::to_vector();
  CHECK(result == input);
}

TEST_CASE("not_in with all elements excluded returns empty") {
  const klspw::string_set excluded = {"a", "b"};
  const klspw::strings input = {"a", "b"};
  const auto result = input | klspw::not_in(excluded) | klspw::to_vector();
  CHECK(result.empty());
}

TEST_CASE("not_in on empty input returns empty") {
  const klspw::string_set excluded = {"a"};
  const klspw::strings input;
  const auto result = input | klspw::not_in(excluded) | klspw::to_vector();
  CHECK(result.empty());
}
