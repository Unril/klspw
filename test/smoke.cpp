#include <doctest/doctest.h>

#include "workspace.hpp"

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

TEST_CASE("ModuleData type has default") {
    klspw::ModuleData m;
    m.name = "mymod";
    CHECK(m.type == "JAVA_MODULE");
}
