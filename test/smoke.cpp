#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "workspace_model.hpp"

TEST_CASE("DependencyData variant holds correct types") {
    const klspw::DependencyData mod = klspw::ModuleDep{.name = "core"};
    CHECK(std::holds_alternative<klspw::ModuleDep>(mod));

    const klspw::DependencyData lib = klspw::LibraryDep{.name = "guava"};
    CHECK(std::holds_alternative<klspw::LibraryDep>(lib));

    const klspw::DependencyData isdk = klspw::InheritedSdk{};
    CHECK(std::holds_alternative<klspw::InheritedSdk>(isdk));

    const klspw::DependencyData msrc = klspw::ModuleSource{};
    CHECK(std::holds_alternative<klspw::ModuleSource>(msrc));

    const klspw::DependencyData sdk = klspw::SdkDep{.name = "JDK21", .kind = "JavaSDK"};
    CHECK(std::holds_alternative<klspw::SdkDep>(sdk));
}

TEST_CASE("ModuleData type is optional") {
    klspw::ModuleData m;
    m.name = "mymod";
    CHECK_FALSE(m.type.has_value());
    m.type = "JAVA_MODULE";
    CHECK(m.type.value() == "JAVA_MODULE");
}
