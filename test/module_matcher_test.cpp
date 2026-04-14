#include "module_matcher.hpp"

#include <doctest/doctest.h>

// --- ModuleMatcher::matches_name ---

TEST_CASE("matches_name exact match") { CHECK(klspw::ModuleMatcher::matches_name("CoreLib", "CoreLib")); }

TEST_CASE("matches_name prefix with version suffix") {
  CHECK(klspw::ModuleMatcher::matches_name("CoreLib-1.0", "CoreLib"));
}

TEST_CASE("matches_name multi-dash module with version") {
  CHECK(klspw::ModuleMatcher::matches_name("My-Cool-Lib-2.3.1", "My-Cool-Lib"));
}

TEST_CASE("matches_name rejects partial prefix without dash") {
  CHECK_FALSE(klspw::ModuleMatcher::matches_name("CoreLibExtra", "CoreLib"));
}

TEST_CASE("matches_name rejects shorter lib name") {
  CHECK_FALSE(klspw::ModuleMatcher::matches_name("Core", "CoreLib"));
}

TEST_CASE("matches_name rejects unrelated names") {
  CHECK_FALSE(klspw::ModuleMatcher::matches_name("guava-33.0", "CoreLib"));
}

TEST_CASE("matches_name jetified prefix strips and matches") {
  CHECK(klspw::ModuleMatcher::matches_name("jetified-CoreLib-1.0", "CoreLib"));
}

TEST_CASE("matches_name jetified exact match") {
  CHECK(klspw::ModuleMatcher::matches_name("jetified-CoreLib", "CoreLib"));
}

TEST_CASE("matches_name jetified rejects unrelated") {
  CHECK_FALSE(klspw::ModuleMatcher::matches_name("jetified-guava-33.0", "CoreLib"));
}

TEST_CASE("matches_name empty strings") {
  CHECK(klspw::ModuleMatcher::matches_name("", ""));
  CHECK_FALSE(klspw::ModuleMatcher::matches_name("CoreLib", ""));
  CHECK_FALSE(klspw::ModuleMatcher::matches_name("", "CoreLib"));
}

// --- ModuleMatcher::find_module ---

TEST_CASE("find_module returns matching module name") {
  const klspw::ModuleMatcher matcher({"CoreLib", "App"});
  CHECK(matcher.find_module("CoreLib-1.0") == "CoreLib");
}

TEST_CASE("find_module returns nullopt for unrelated library") {
  const klspw::ModuleMatcher matcher({"CoreLib"});
  CHECK_FALSE(matcher.find_module("guava-33.0").has_value());
}

TEST_CASE("find_module prefers longest match") {
  const klspw::ModuleMatcher matcher({"Core", "CoreLib"});
  CHECK(matcher.find_module("CoreLib-1.0") == "CoreLib");
}

TEST_CASE("find_module matches jetified library") {
  const klspw::ModuleMatcher matcher({"public"});
  CHECK(matcher.find_module("jetified-public-release-api") == "public");
}

TEST_CASE("find_module matches Maven coordinate module component") {
  const klspw::ModuleMatcher matcher({"RabbitAndroidFramework", "CoreLib"});
  CHECK(matcher.find_module("com.example.mobile:RabbitAndroidFramework:1.0") == "RabbitAndroidFramework");
  CHECK(matcher.find_module("org.example:CoreLib:2.0.0") == "CoreLib");
}

TEST_CASE("find_module matches Maven coordinate with prefix-dash module") {
  const klspw::ModuleMatcher matcher({"presenter-public"});
  CHECK(matcher.find_module("software.acme:presenter-public-android:0.0.7") == "presenter-public");
}

TEST_CASE("find_module rejects Maven coordinate when module component doesn't match") {
  const klspw::ModuleMatcher matcher({"CoreLib"});
  CHECK_FALSE(matcher.find_module("com.example:unrelated:1.0").has_value());
}

TEST_CASE("find_module empty matcher returns nullopt") {
  const klspw::ModuleMatcher matcher({});
  CHECK_FALSE(matcher.find_module("CoreLib-1.0").has_value());
}

// --- ModuleMatcher::classpath_matches ---

TEST_CASE("classpath_matches jar path with version") {
  const klspw::ModuleMatcher matcher({"kotlin-stdlib"});
  CHECK(matcher.classpath_matches("/cache/kotlin-stdlib-2.0.0.jar"));
}

TEST_CASE("classpath_matches rejects unrelated jar") {
  const klspw::ModuleMatcher matcher({"CoreLib"});
  CHECK_FALSE(matcher.classpath_matches("/cache/guava-33.0.jar"));
}

TEST_CASE("classpath_matches directory path") {
  const klspw::ModuleMatcher matcher({"mymodule"});
  CHECK(matcher.classpath_matches("/build/libs/mymodule-1.0"));
}

TEST_CASE("classpath_matches empty matcher never matches") {
  const klspw::ModuleMatcher matcher({});
  CHECK_FALSE(matcher.classpath_matches("/cache/anything.jar"));
}

// --- ModuleMatcher::promote_target ---

TEST_CASE("promote_target returns module name for matching library") {
  const klspw::ModuleMatcher matcher({"CoreLib", "App"});
  CHECK(matcher.promote_target("CoreLib-1.0", "App") == "CoreLib");
}

TEST_CASE("promote_target returns nullopt for self-reference") {
  const klspw::ModuleMatcher matcher({"CoreLib"});
  CHECK_FALSE(matcher.promote_target("CoreLib-1.0", "CoreLib").has_value());
}

TEST_CASE("promote_target returns nullopt for unrelated library") {
  const klspw::ModuleMatcher matcher({"CoreLib"});
  CHECK_FALSE(matcher.promote_target("guava-33.0", "App").has_value());
}

TEST_CASE("promote_target handles jetified self-reference") {
  const klspw::ModuleMatcher matcher({"public"});
  CHECK_FALSE(matcher.promote_target("jetified-public-release-api", "public").has_value());
}

TEST_CASE("promote_target prefers longest match over self") {
  const klspw::ModuleMatcher matcher({"Core", "CoreLib"});
  CHECK(matcher.promote_target("CoreLib-1.0", "Core") == "CoreLib");
}
