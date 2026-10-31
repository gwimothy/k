#include "core/cvar.h"

#include <doctest/doctest.h>

using namespace k;

TEST_CASE("ConVar parses and clamps values") {
    ConVar v("test_cvar_a", "10", FCVAR_NONE, "help", 0.0f, 100.0f);
    CHECK(v.getInt() == 10);
    CHECK(v.getFloat() == doctest::Approx(10.0f));
    CHECK(v.getString() == "10");

    v.setValue("2.5");
    CHECK(v.getFloat() == doctest::Approx(2.5f));
    CHECK(v.getInt() == 2);

    v.setValue("500");
    CHECK(v.getFloat() == doctest::Approx(100.0f));
    CHECK(v.getString() == "100");

    v.setValue(-3);
    CHECK(v.getInt() == 0);

    v.revert();
    CHECK(v.getString() == "10");
}

TEST_CASE("ConVar change callback fires only on change") {
    int calls = 0;
    std::string lastOld;
    ConVar v("test_cvar_cb", "1", FCVAR_NONE, {}, std::nullopt, std::nullopt,
             [&](ConVar&, const std::string& old) {
                 ++calls;
                 lastOld = old;
             });
    v.setValue("1");
    CHECK(calls == 0);
    v.setValue("2");
    CHECK(calls == 1);
    CHECK(lastOld == "1");
}

TEST_CASE("Lookup is case-insensitive and unregisters on destruction") {
    {
        ConVar v("Test_Cvar_Scoped", "0");
        CHECK(console::findVar("test_cvar_scoped") == &v);
        CHECK(console::findCommand("test_cvar_scoped") == nullptr);
    }
    CHECK(console::find("test_cvar_scoped") == nullptr);
}

TEST_CASE("Cheat cvars respect the cheat gate") {
    bool cheats = false;
    console::setCheatGate([&] { return cheats; });
    ConVar v("test_cvar_cheat", "1", FCVAR_CHEAT);
    v.setValue("5");
    CHECK(v.getInt() == 1);
    cheats = true;
    v.setValue("5");
    CHECK(v.getInt() == 5);
    console::setCheatGate(nullptr);
}

TEST_CASE("Name completion") {
    ConVar a("test_complete_alpha", "0"), b("test_complete_beta", "0");
    auto m = console::completeName("TEST_COMPLETE_");
    REQUIRE(m.size() == 2);
    CHECK(m[0] == "test_complete_alpha");
    CHECK(m[1] == "test_complete_beta");
}
