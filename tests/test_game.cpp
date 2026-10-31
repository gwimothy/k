#include "game_world.h"
#include "shared/bsp_trace.h"
#include "test_room.h"

#include <doctest/doctest.h>

#include <memory>

using namespace k;
using namespace k::game;

namespace {

constexpr float kDt = 1.0f / 66.0f;

// A game world with captured point_message output.
struct TestGame {
    std::vector<std::string> messages;
    std::unique_ptr<GameWorld> world;

    explicit TestGame(bool withMap) {
        EngineServices services;
        services.print = [this](const std::string& m) { messages.push_back(m); };
        services.centerPrint = services.print;
        world = std::make_unique<GameWorld>(services);
        std::shared_ptr<const bsp::BspData> map;
        if (withMap) map = std::make_shared<const bsp::BspData>(k::test::testRoom());
        world->levelInit("test", map);
        if (!withMap) spawn({{"classname", "point_message"}, {"targetname", "msg"}});
    }
    BaseEntity* spawn(const bsp::Entity& e) { return world->spawnEntity(e); }
    void run(float seconds, UserCmd cmd = {}) {
        int ticks = static_cast<int>(seconds / kDt + 0.5f);
        for (int i = 0; i < ticks; ++i) {
            world->runCommand(cmd, kDt);
            world->frame(kDt);
        }
    }
    bool said(const std::string& m) const { return std::find(messages.begin(), messages.end(), m) != messages.end(); }
    size_t count(const std::string& m) const { return static_cast<size_t>(std::count(messages.begin(), messages.end(), m)); }
};

} // namespace

TEST_CASE("io: connection parsing") {
    Connection c;
    REQUIRE(parseConnection("door1,Open,,1.5,-1", c));
    CHECK(c.target == "door1");
    CHECK(c.input == "Open");
    CHECK(c.param.empty());
    CHECK(c.delay == doctest::Approx(1.5f));
    CHECK(c.timesToFire == -1);
    REQUIRE(parseConnection("msg\x1bShowMessage\x1bhello\x1b" "0\x1b" "1", c)); // Source's ESC separator
    CHECK(c.param == "hello");
    CHECK(c.timesToFire == 1);
    CHECK_FALSE(parseConnection("justatarget", c));
    CHECK(nameMatches("door*", "Door12"));
    CHECK_FALSE(nameMatches("door*", "lift"));
}

TEST_CASE("io: relay chains with delays, parameters and fire limits") {
    TestGame g(false);
    g.spawn({{"classname", "logic_relay"}, {"targetname", "r1"}, {"OnTrigger", "r2,Trigger,,0.5,-1"},
             {"OnTrigger#2", "msg,ShowMessage,once,0,1"}});
    g.spawn({{"classname", "logic_relay"}, {"targetname", "r2"}, {"OnTrigger", "msg,ShowMessage,chained,0,-1"}});

    g.world->deliverInput("r1", "Trigger", "", nullptr, nullptr);
    g.run(0.1f);
    CHECK(g.said("once"));
    CHECK_FALSE(g.said("chained")); // still delayed
    g.run(0.5f);
    CHECK(g.said("chained"));

    g.world->deliverInput("r1", "Trigger", "", nullptr, nullptr);
    g.run(0.6f);
    CHECK(g.count("once") == 1);    // times-to-fire 1
    CHECK(g.count("chained") == 2);

    // Disabled relays ignore Trigger.
    g.world->deliverInput("r2", "Disable", "", nullptr, nullptr);
    g.world->deliverInput("r2", "Trigger", "", nullptr, nullptr);
    g.run(0.1f);
    CHECK(g.count("chained") == 2);
}

TEST_CASE("io: wildcards, !self and Kill") {
    TestGame g(false);
    g.spawn({{"classname", "logic_relay"}, {"targetname", "a1"}, {"OnTrigger", "msg,ShowMessage,a,0,-1"}});
    g.spawn({{"classname", "logic_relay"}, {"targetname", "a2"}, {"OnTrigger", "msg,ShowMessage,a,0,-1"}});
    auto* self = g.spawn({{"classname", "logic_relay"}, {"targetname", "killer"}, {"OnTrigger", "!self,Kill,,0,-1"}});
    int killerIndex = self->index;
    g.world->deliverInput("a*", "Trigger", "", nullptr, nullptr);
    g.world->deliverInput("killer", "Trigger", "", nullptr, nullptr);
    g.run(0.1f);
    CHECK(g.count("a") == 2);
    CHECK(g.world->entity(killerIndex) == nullptr);
}

TEST_CASE("io: logic_timer and math_counter") {
    TestGame g2(false);
    g2.spawn({{"classname", "math_counter"}, {"targetname", "count"}, {"min", "0"}, {"max", "3"},
              {"OnHitMax", "msg,ShowMessage,max,0,-1"}, {"OutValue", "msg,ShowMessage,,0,-1"}});
    auto* timer = g2.spawn({{"classname", "logic_timer"}, {"targetname", "t"}, {"RefireTime", "0.25"},
                            {"OnTimer", "count,Add,1,0,-1"}});
    timer->activate();
    g2.run(0.6f);
    CHECK(g2.said("1"));
    CHECK(g2.said("2"));
    CHECK_FALSE(g2.said("max"));
    g2.run(0.4f);
    CHECK(g2.said("3"));
    CHECK(g2.count("max") == 1);
    g2.world->deliverInput("t", "Disable", "", nullptr, nullptr);
    g2.world->deliverInput("count", "SetValue", "0", nullptr, nullptr);
    g2.run(1.0f);
    CHECK(g2.count("1") == 1); // timer stopped: no more increments
}

TEST_CASE("game: map entities spawn from the BSP") {
    TestGame g(true);
    CHECK(g.world->findFirstByName("door1") != nullptr);
    CHECK(g.world->findFirstByName("button1") != nullptr);
    CHECK(g.world->findFirstByName("lift") != nullptr);
    CHECK(g.world->playerState().origin.x == doctest::Approx(128.0f));
    // Brush entities are networked with "*N" models.
    std::vector<proto::EntityState> states;
    g.world->buildEntityStates(states);
    CHECK(states.size() >= 3);
    bool found = false;
    for (auto& m : g.world->modelTable()) found |= !m.empty() && m[0] == '*';
    CHECK(found);
}

TEST_CASE("game: doors block until opened, then move out of the way") {
    TestGame g(true);
    auto* door = g.world->findFirstByName("door1");
    REQUIRE(door);
    // The door slab is at x 64..80. A hull trace across it is blocked by the entity.
    auto tr = g.world->traceBox({0, 150, 0}, {150, 150, 0}, kHullMins, kHullMaxs, kPlayerMask);
    CHECK(tr.fraction < 1.0f);
    CHECK(tr.entity == door->index);

    g.world->deliverInput("door1", "Open", "", nullptr, nullptr);
    g.run(0.6f); // 120 units at 400 u/s
    CHECK(door->origin.z == doctest::Approx(120.0f)); // height 128 - lip 8
    CHECK(g.said("door open"));
    // Raised by 120: the hull (72 tall) now passes underneath.
    tr = g.world->traceBox({0, 150, 0}, {150, 150, 0}, kHullMins, kHullMaxs, kPlayerMask);
    CHECK(tr.fraction == 1.0f);
}

TEST_CASE("game: triggers fire on enter and leave") {
    TestGame g(true);
    g.world->setPlayerOrigin({-175, 125, 0});
    g.run(0.1f);
    CHECK(g.said("triggered"));
    g.world->setPlayerOrigin({0, -150, 0});
    g.run(0.1f);
    CHECK(g.said("left"));
}

TEST_CASE("game: +use presses a button the player is looking at") {
    TestGame g(true);
    g.world->setPlayerOrigin({200, 56, 0}); // eyes at z 64, button at z 48..80 on the east wall
    UserCmd look;
    look.viewAngles = {0, 0, 0};
    g.run(0.05f, look);
    UserCmd use = look;
    use.buttons = IN_USE;
    g.run(0.05f, use);
    CHECK(g.said("pressed"));
    // Looking away: nothing.
    TestGame g2(true);
    g2.world->setPlayerOrigin({200, 56, 0});
    UserCmd away;
    away.viewAngles = {0, 180, 0};
    g2.run(0.05f, away);
    away.buttons = IN_USE;
    g2.run(0.05f, away);
    CHECK_FALSE(g2.said("pressed"));
}

TEST_CASE("game: elevators carry the player") {
    TestGame g(true);
    g.world->setPlayerOrigin({-184, -32, 9}); // standing on the 8-unit lift platform
    g.run(0.3f);
    REQUIRE(g.world->playerState().groundEntity == g.world->findFirstByName("lift")->index);
    float before = g.world->playerState().origin.z;
    g.world->deliverInput("lift", "Open", "", nullptr, nullptr);
    g.run(1.0f); // rises 64 units at 100 u/s
    auto* lift = g.world->findFirstByName("lift");
    CHECK(lift->origin.z == doctest::Approx(64.0f));
    CHECK(g.world->playerState().origin.z == doctest::Approx(before + 64.0f).epsilon(0.02));
}
