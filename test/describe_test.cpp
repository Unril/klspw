#include "describe.hpp"

#include <doctest/doctest.h>

#include "test_common.hpp"

using namespace klspw;

namespace {

struct TestItem {
  std::string name;

  void describe() const { d_info("item: {}", name); }
};

}  // namespace

// --- d_info / d_debug / d_trace ---

TEST_CASE("d_info logs at info level") {
  const LogCapture log;
  d_info("hello {}", "world");
  CHECK(log.output().contains("hello world"));
}

TEST_CASE("d_debug logs at debug level") {
  const LogCapture log;
  d_debug("count: {}", 42);
  CHECK(log.output().contains("count: 42"));
}

TEST_CASE("d_trace logs at trace level") {
  const LogCapture log;
  d_trace("detail: {}", "x");
  CHECK(log.output().contains("detail: x"));
}

// --- d_warn / d_error / d_critical ---

TEST_CASE("d_warn logs at warn level") {
  const LogCapture log;
  d_warn("missing: {}", "file.txt");
  CHECK(log.output().contains("missing: file.txt"));
}

TEST_CASE("d_error logs at error level") {
  const LogCapture log;
  d_error("failed: {}", "op");
  CHECK(log.output().contains("failed: op"));
}

TEST_CASE("d_critical logs at critical level") {
  const LogCapture log;
  d_critical("fatal: {}", "crash");
  CHECK(log.output().contains("fatal: crash"));
}

// --- lazy evaluation ---

TEST_CASE("d_info evaluates lazy path arg") {
  const LogCapture log;
  d_info("path: {}", std::filesystem::path{"/tmp/test"});
  CHECK(log.output().contains("/tmp/test"));
}

TEST_CASE("d_debug skips lazy evaluation when level is too low") {
  const LogCapture log;
  spdlog::set_level(spdlog::level::warn);
  int call_count = 0;
  auto lazy = [&] {
    ++call_count;
    return "expensive";
  };
  d_debug("val: {}", lazy);
  CHECK(call_count == 0);
  CHECK(log.output().empty());
}

// --- d_describe ---

TEST_CASE("d_describe calls describe on a Describable") {
  const LogCapture log;
  const TestItem item{.name = "foo"};
  d_describe(item);
  CHECK(log.output().contains("item: foo"));
}

TEST_CASE("d_describe on optional with value calls describe") {
  const LogCapture log;
  const optional<TestItem> item = TestItem{.name = "bar"};
  d_describe(item);
  CHECK(log.output().contains("item: bar"));
}

TEST_CASE("d_describe on empty optional is a no-op") {
  const LogCapture log;
  const optional<TestItem> item;
  d_describe(item);
  CHECK(log.output().empty());
}

TEST_CASE("d_describe on range calls describe for each element") {
  const LogCapture log;
  const std::vector<TestItem> items = {{.name = "a"}, {.name = "b"}};
  d_describe(items);
  const auto out = log.output();
  CHECK(out.contains("item: a"));
  CHECK(out.contains("item: b"));
}

// --- Describable concept ---

static_assert(Describable<TestItem>);
static_assert(!Describable<std::string>);
