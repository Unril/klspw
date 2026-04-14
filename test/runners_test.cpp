#include <doctest/doctest.h>

#include "gradle_runner.hpp"
#include "process_runner.hpp"
#include "test_common.hpp"

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
  CHECK_THROWS_WITH_AS(ProcessRunner({"sh", "-c", "echo oops && exit 1"}).run(), doctest::Contains("oops"),
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

// --- GradleRunner ---

TEST_CASE("GradleRunner writes init script to specified temp dir") {
  const TempDir tmp;
  const klspw::GradleRunner runner(tmp.path);

  const auto& path = runner.init_script_path();
  REQUIRE(fs::exists(path));
  CHECK(path.string().starts_with(tmp.path.string()));
  CHECK(path.extension() == ".kts");
}

TEST_CASE("init script contains expected markers") {
  const TempDir tmp;
  const klspw::GradleRunner runner(tmp.path);

  const auto content = klspw::read_file(runner.init_script_path());

  CHECK(content.contains("dumpKotlinLspModel"));
  CHECK(content.contains("KLSPW_BEGIN"));
  CHECK(content.contains("KLSPW_END"));
  CHECK(content.contains("JsonOutput"));
}

TEST_CASE("init script is cleaned up on destruction") {
  const TempDir tmp;
  fs::path script_path;
  {
    const klspw::GradleRunner runner(tmp.path);
    script_path = runner.init_script_path();
    REQUIRE(fs::exists(script_path));
  }
  CHECK_FALSE(fs::exists(script_path));
}

TEST_CASE("GradleRunner uses default temp dir when not specified") {
  const klspw::GradleRunner runner;

  const auto& path = runner.init_script_path();
  REQUIRE(fs::exists(path));
  CHECK(path.string().contains("klspw"));
}

TEST_CASE("GradleRunner close is idempotent") {
  const TempDir tmp;
  klspw::GradleRunner runner(tmp.path);
  const auto script = runner.init_script_path();
  REQUIRE(fs::exists(script));

  runner.close();
  CHECK_FALSE(fs::exists(script));
  CHECK(runner.init_script_path().empty());

  // second close is a no-op, must not throw
  CHECK_NOTHROW(runner.close());
}

TEST_CASE("GradleRunner move constructor transfers ownership") {
  const TempDir tmp;
  klspw::GradleRunner original(tmp.path);
  const auto script = original.init_script_path();
  REQUIRE(fs::exists(script));

  const klspw::GradleRunner moved(std::move(original));

  CHECK(moved.init_script_path() == script);
  CHECK(fs::exists(script));
  CHECK(original.init_script_path().empty());  // NOLINT(bugprone-use-after-move)
}

TEST_CASE("GradleRunner move assignment transfers ownership and cleans old") {
  const TempDir tmp_a;
  const TempDir tmp_b;
  klspw::GradleRunner a(tmp_a.path);
  klspw::GradleRunner b(tmp_b.path);
  const auto script_a = a.init_script_path();
  const auto script_b = b.init_script_path();
  REQUIRE(script_a != script_b);

  b = std::move(a);

  CHECK(b.init_script_path() == script_a);
  CHECK(fs::exists(script_a));
  CHECK_FALSE(fs::exists(script_b));  // old b's script cleaned up
  CHECK(a.init_script_path().empty());  // NOLINT(bugprone-use-after-move)
}

TEST_CASE("GradleRunner operator() assembles correct args and runs process") {
  const TempDir tmp;
  const klspw::GradleRunner runner(tmp.path);

  // Use a BuildConfig that runs 'echo' instead of Gradle — verifies arg assembly + ProcessRunner delegation
  const klspw::BuildConfig build{.command = {"echo"}, .gradle_args = {"--quiet"}};
  const auto output = runner(build, "/tmp");

  // echo receives: --no-configuration-cache --init-script {path} --quiet dumpKotlinLspModel
  CHECK(output.contains("--no-configuration-cache"));
  CHECK(output.contains("--init-script"));
  CHECK(output.contains(runner.init_script_path().string()));
  CHECK(output.contains("--quiet"));
  CHECK(output.contains("dumpKotlinLspModel"));
}
