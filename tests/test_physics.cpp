#include "core/filesystem.h"
#include "game_world.h"
#include "physics.h"
#include "test_room.h"

#include <doctest/doctest.h>

using namespace k;
using namespace k::game;

namespace {

constexpr float kDt = 1.0f / 66.0f;

struct PhysGame {
    std::unique_ptr<GameWorld> world;
    PhysGame() {
        fs::clearSearchPaths();
        fs::addSearchPath(K_SOURCE_DIR "/base", "GAME");
        world = std::make_unique<GameWorld>(EngineServices{});
        world->levelInit("test", std::make_shared<const bsp::BspData>(k::test::testRoom()));
        world->clientConnect(0, "tester");
    }
    ~PhysGame() { fs::clearSearchPaths(); }
    BaseEntity* crate(const Vec3& origin) {
        return world->spawnEntity({{"classname", "prop_physics"}, {"model", "models/props/crate.glb"},
                                   {"origin", std::to_string(origin.x) + " " + std::to_string(origin.y) + " " +
                                                  std::to_string(origin.z)}});
    }
    void run(float seconds, UserCmd cmd = {}) {
        for (int i = 0; i < static_cast<int>(seconds / kDt + 0.5f); ++i) {
            world->runCommand(0, cmd, kDt);
            world->frame(kDt, 0);
        }
    }
};

} // namespace

TEST_CASE("physics: brush vertices from planes") {
    const auto& b = k::test::testRoom();
    // The pillar is the 7th world brush (a 64x64x256 box): find a brush with 8 corners.
    bool found = false;
    for (auto& br : b.brushes) {
        auto v = brushVertices(b, br);
        if (v.size() == 8) found = true;
        CHECK(v.size() >= 6); // wedges have 6
    }
    CHECK(found);
}

TEST_CASE("physics: a crate falls and comes to rest on the floor") {
    PhysGame g;
    auto* c = g.crate({-100, 0, 120});
    REQUIRE(c);
    REQUIRE(c->physicsDriven);
    g.run(2.5f);
    CHECK(c->origin.z == doctest::Approx(0.0f).epsilon(0.02));
    CHECK(std::fabs(c->angles.pitch) < 2.0f);
    CHECK(std::fabs(c->angles.roll) < 2.0f);
    CHECK(g.world->physics()->mass(c->physicsBody) == doctest::Approx(32.0f).epsilon(0.1));
}

TEST_CASE("physics: the player is blocked by and pushes a crate") {
    PhysGame g;
    auto* c = g.crate({0, -150, 0});
    g.run(0.5f);
    float startX = c->origin.x;
    g.world->setPlayerOrigin(0, {-120, -150, 0});
    UserCmd walk;
    walk.forwardMove = 450; // east, into the crate
    g.run(1.5f, walk);
    // The player didn't walk through it...
    CHECK(g.world->playerState(0).origin.x < c->origin.x);
    // ...and it moved.
    CHECK(c->origin.x > startX + 20.0f);
}

TEST_CASE("physics: +use picks a crate up, +attack throws it") {
    PhysGame g;
    auto* c = g.crate({160, 0, 0});
    g.run(0.5f);
    g.world->setPlayerOrigin(0, {100, 0, 0});
    UserCmd look;
    look.viewAngles = {30, 0, 0}; // look down at it
    g.run(0.1f, look);
    UserCmd use = look;
    use.buttons = IN_USE;
    g.run(0.05f, use);
    look.viewAngles = {0, 0, 0};
    g.run(0.8f, look);
    CHECK(c->origin.z > 20.0f); // lifted off the floor
    UserCmd throwIt = look;
    throwIt.buttons = IN_ATTACK;
    g.run(0.05f, throwIt);
    CHECK(g.world->physics()->linearVelocity(c->physicsBody).x > 300.0f);
}

TEST_CASE("physics: a crate rides the lift") {
    PhysGame g;
    auto* c = g.crate({-184, -32, 12}); // on the lift platform (x -232..-136, y -80..16, top z 8)
    g.run(1.0f);
    float before = c->origin.z;
    g.world->deliverInput("lift", "Open", "", nullptr, nullptr);
    g.run(1.5f);
    CHECK(c->origin.z == doctest::Approx(before + 64.0f).epsilon(0.05));
}
