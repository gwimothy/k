#include "core/keyvalues.h"

#include <doctest/doctest.h>

using namespace k;

TEST_CASE("KeyValues parses a material") {
    const char* text = R"(
// a comment
"LitGeneric"
{
    "$basetexture" "dev/dev_orange"   // trailing comment
    $color "[1 0.5 0.25]"
    "$alpha" ".5"
    "$nocull" 1
    "Proxies"
    {
        "Spin" { "rate" "90" }
    }
}
)";
    std::string error;
    auto kv = KeyValues::parse(text, &error);
    REQUIRE_MESSAGE(kv, error);
    REQUIRE(kv->children().size() == 1);
    const auto& m = kv->children()[0];
    CHECK(m.name() == "LitGeneric");
    CHECK(m.isSection());
    CHECK(m.getString("$BaseTexture") == "dev/dev_orange");
    Vec3 c = m.getVec3("$color");
    CHECK(c.y == doctest::Approx(0.5f));
    CHECK(c.z == doctest::Approx(0.25f));
    CHECK(m.getFloat("$alpha") == doctest::Approx(0.5f));
    CHECK(m.getBool("$nocull"));
    CHECK(m.getInt("missing", 7) == 7);
    auto* spin = m.find("proxies") ? m.find("proxies")->find("spin") : nullptr;
    REQUIRE(spin);
    CHECK(spin->getInt("rate") == 90);
}

TEST_CASE("KeyValues reports errors with line numbers") {
    std::string error;
    CHECK_FALSE(KeyValues::parse("\"a\"\n{\n \"b\" \"c\"\n", &error));
    CHECK(error.find("missing '}'") != std::string::npos);
    CHECK_FALSE(KeyValues::parse("\"a\" \"unterminated", &error));
    CHECK_FALSE(KeyValues::parse("}", &error));
}

TEST_CASE("KeyValues round-trips through serialize") {
    KeyValues root;
    auto& s = root.addSection("Root");
    s.add("key", "value with spaces");
    s.addSection("child").add("x", "1");
    auto parsed = KeyValues::parse(root.serialize());
    REQUIRE(parsed);
    CHECK(parsed->find("root")->getString("key") == "value with spaces");
    CHECK(parsed->find("root")->find("child")->getInt("x") == 1);
}

TEST_CASE("parseVec3 accepts several formats") {
    CHECK(parseVec3("1 2 3") == Vec3(1, 2, 3));
    CHECK(parseVec3("[4 5 6]") == Vec3(4, 5, 6));
    CHECK(parseVec3("{7,8,9}") == Vec3(7, 8, 9));
    CHECK(parseVec3("1", Vec3(0, 9, 9)) == Vec3(1, 9, 9));
}
