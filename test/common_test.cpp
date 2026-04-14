#include "common.hpp"

#include <system_error>

#include <doctest/doctest.h>

using namespace klspw;

// --- require ---

TEST_CASE("require does not throw when condition is true") { CHECK_NOTHROW(klspw::require(true, "should not throw")); }

TEST_CASE("require throws when condition is false") {
  CHECK_THROWS_AS(klspw::require(false, "expected failure"), std::runtime_error);
}

TEST_CASE("require formats message with args") {
  CHECK_THROWS_WITH_AS(klspw::require(false, "value is {}", 42), doctest::Contains("42"), std::runtime_error);
}

TEST_CASE("require auto-converts path") {
  CHECK_THROWS_WITH_AS(klspw::require(false, "path: {}", std::filesystem::path{"/tmp/test"}),
                       doctest::Contains("/tmp/test"), std::runtime_error);
}

TEST_CASE("require evaluates callable lazily") {
  int call_count = 0;
  auto lazy = [&] {
    ++call_count;
    return "lazy value";
  };

  klspw::require(true, "msg: {}", lazy);
  CHECK(call_count == 0);  // not called on success

  CHECK_THROWS_AS(klspw::require(false, "msg: {}", lazy), std::runtime_error);
  CHECK(call_count == 1);  // called on failure
}

TEST_CASE("require auto-converts error_code") {
  const auto ec = std::make_error_code(std::errc::no_such_file_or_directory);
  CHECK_THROWS_WITH_AS(klspw::require(false, "error: {}", ec), doctest::Contains("No such file"), std::runtime_error);
}
