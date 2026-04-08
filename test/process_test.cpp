#include <doctest/doctest.h>

#include "process.hpp"

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
    try {
        ProcessRunner({"sh", "-c", "exit 7"}).run();
        FAIL("Expected exception");
    } catch (const std::runtime_error& e) {
        const string msg = e.what();
        CHECK(msg.contains("7"));
        CHECK(msg.contains("sh"));
    }
}

TEST_CASE("error message includes stdout from failing command") {
    try {
        ProcessRunner({"sh", "-c", "echo oops && exit 1"}).run();
        FAIL("Expected exception");
    } catch (const std::runtime_error& e) {
        CHECK(string(e.what()).contains("oops"));
    }
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
