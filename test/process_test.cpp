#include <doctest/doctest.h>

#include "process_runner.hpp"

using namespace klspw;

TEST_CASE("runs echo and captures stdout") {
    auto output = ProcessRunner({"echo", "hello world"}).run();
    CHECK(trim(output) == "hello world");
}

TEST_CASE("captures multi-line output") {
    auto output = ProcessRunner({"printf", "line1\\nline2\\nline3"}).run();
    CHECK(output.contains("line1"));
    CHECK(output.contains("line3"));
}

TEST_CASE("throws on non-zero exit code") {
    CHECK_THROWS_AS(ProcessRunner({"sh", "-c", "exit 42"}).run(), std::runtime_error);
}

TEST_CASE("error message includes exit code and command") {
    CHECK_THROWS_WITH_AS(ProcessRunner({"sh", "-c", "exit 7"}).run(), doctest::Contains("7"), std::runtime_error);
    CHECK_THROWS_WITH_AS(ProcessRunner({"sh", "-c", "exit 7"}).run(), doctest::Contains("sh"), std::runtime_error);
}

TEST_CASE("error message includes stdout from failing command") {
    CHECK_THROWS_WITH_AS(ProcessRunner({"sh", "-c", "echo oops && exit 1"}).run(),
        doctest::Contains("oops"),
        std::runtime_error);
}

TEST_CASE("throws on nonexistent command") {
    CHECK_THROWS_AS(ProcessRunner({"__nonexistent_command_12345__"}).run(), std::runtime_error);
}

TEST_CASE("handles empty stdout") {
    auto output = ProcessRunner({"true"}).run();
    CHECK(output.empty());
}

TEST_CASE("captures large output") {
    auto output = ProcessRunner({"sh", "-c", "seq 1 1000"}).run();
    CHECK(output.contains("1000"));
}

TEST_CASE("runs command with multiple arguments") {
    auto output = ProcessRunner({"echo", "hello", "world"}).run();
    CHECK(trim(output) == "hello world");
}

TEST_CASE("args() returns the command") {
    const ProcessRunner runner({"echo", "test"});
    REQUIRE(runner.args().size() == 2);
    CHECK(runner.args()[0] == "echo");
}

TEST_CASE("runs command in specified working directory") {
    auto output = ProcessRunner({"pwd"}, "/tmp").run();
    // macOS: /tmp -> /private/tmp
    CHECK(trim(output).ends_with("/tmp"));
}

TEST_CASE("cwd accessor returns the working directory") {
    const ProcessRunner runner({"echo"}, "/tmp");
    CHECK(runner.cwd() == "/tmp");
}

TEST_CASE("cwd defaults to empty") {
    const ProcessRunner runner({"echo"});
    CHECK(runner.cwd().empty());
}
