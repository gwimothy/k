#include "game_world.h"
#include "test_room.h"

#include <doctest/doctest.h>

using namespace k;
using namespace k::game;

namespace {
constexpr float kDt = 1.0f / 66.0f;
}

TEST_CASE("combat: fall damage follows Source's curve") {
    GameWorld world{EngineServices{}};
    world.levelInit("test", std::make_shared<const bsp::BspData>(k::test::testRoom()));
    world.clientConnect(0, "faller");
    world.setPlayerOrigin(0, {100, 100, 180}); // the room is 256 tall; hull is 72
    for (int i = 0; i < 132; ++i) {
        world.runCommand(0, UserCmd{}, kDt);
        world.frame(kDt, static_cast<uint32_t>(i));
    }
    // A 180 unit drop lands at ~537 u/s: just past the 526.5 safe limit -> a few points.
    int health = world.playerState(0).health;
    CHECK(health < 100);
    CHECK(health > 90);
}

TEST_CASE("combat: hitscan damage, death, kill feed and respawn") {
    std::vector<std::string> feed;
    int confirms = 0;
    EngineServices services;
    services.killFeed = [&](const std::string& a, const std::string& v, const std::string&) { feed.push_back(a + ">" + v); };
    services.hitConfirm = [&](int) { ++confirms; };
    GameWorld world{services};
    world.levelInit("test", std::make_shared<const bsp::BspData>(k::test::testRoom()));
    world.clientConnect(0, "alice");
    world.clientConnect(1, "bob");
    world.setPlayerOrigin(0, {-200, -120, 0});
    world.setPlayerOrigin(1, {100, -120, 0});
    uint32_t tick = 0;
    auto run = [&](UserCmd a, int ticks) {
        for (int i = 0; i < ticks; ++i) {
            world.runCommand(0, a, kDt);
            world.runCommand(1, UserCmd{}, kDt);
            world.frame(kDt, ++tick);
        }
    };
    UserCmd aim;
    aim.viewAngles = {0, 0, 0};
    for (int shot = 0; shot < 4; ++shot) {
        UserCmd fire = aim;
        fire.buttons = IN_ATTACK;
        fire.tickCount = tick;
        run(fire, 1);
        run(aim, 3); // release so the next press fires
    }
    CHECK(confirms == 4);
    CHECK(world.playerState(1).dead == 1);
    REQUIRE(feed.size() == 1);
    CHECK(feed[0] == "alice>bob");
    // Dead players can't be hit and don't block.
    UserCmd fire = aim;
    fire.buttons = IN_ATTACK;
    run(fire, 1);
    CHECK(confirms == 4);
    // Respawn after sv_respawntime (3 s) at full health.
    run(aim, 220);
    CHECK(world.playerState(1).dead == 0);
    CHECK(world.playerState(1).health == 100);
}

TEST_CASE("combat: trigger_hurt damages players inside it") {
    GameWorld world{EngineServices{}};
    world.levelInit("test", std::make_shared<const bsp::BspData>(k::test::testRoom()));
    world.clientConnect(0, "victim");
    // Make the test room's trigger volume a hurt trigger by spawning one over it.
    auto* hurt = world.spawnEntity({{"classname", "trigger_hurt"}, {"model", "*3"}, {"damage", "40"}});
    REQUIRE(hurt);
    world.setPlayerOrigin(0, {-175, 125, 0});
    for (int i = 0; i < 70; ++i) {
        world.runCommand(0, UserCmd{}, kDt);
        world.frame(kDt, static_cast<uint32_t>(i));
    }
    // ~1 s inside at 40/s, applied every 0.5 s.
    CHECK(world.playerState(0).health <= 70);
    CHECK(world.playerState(0).health >= 40);
}
