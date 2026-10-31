// End-to-end networking in one process: a server listening on localhost UDP and remote
// clients connecting to it, stepped with a simulated clock.

#include "client/client.h"
#include "core/cmdbuffer.h"
#include "core/cvar.h"
#include "core/time.h"
#include "game_world.h"
#include "server/server.h"
#include "test_room.h"

#include <doctest/doctest.h>

#include <random>

using namespace k;

namespace {

struct NetTest {
    std::shared_ptr<const bsp::BspData> map = std::make_shared<const bsp::BspData>(k::test::testRoom());
    Server server;
    std::vector<std::unique_ptr<Client>> clients;
    double now = 0;
    uint16_t port = 0;

    NetTest() {
        std::random_device rd;
        port = static_cast<uint16_t>(28000 + rd() % 2000);
        REQUIRE(server.start("test", map, port));
        port = server.listenPort();
    }
    ~NetTest() {
        for (auto& c : clients) c->disconnect();
        server.stop();
        cmd::executeNow("-forward");
    }
    Client& addClient(const std::string& name) {
        clients.push_back(std::make_unique<Client>());
        Client& c = *clients.back();
        REQUIRE(c.connectRemote(*net::resolveAddress("127.0.0.1:" + std::to_string(port), 0), name));
        return c;
    }
    void step(int ticks) {
        float dt = server.tickInterval();
        for (int i = 0; i < ticks; ++i) {
            now += dt;
            for (auto& c : clients) {
                c->readPackets(platformTime());
                if (auto m = c->takeMapToLoad()) c->setMap(map);
                c->tick(platformTime(), dt);
                c->advance(dt);
            }
            server.tick(platformTime());
            // Real sockets: give packets a moment to cross.
            double until = platformTime() + 0.001;
            while (platformTime() < until) {}
        }
    }
};

} // namespace

TEST_CASE("multiplayer: a remote client connects, moves, and its prediction matches the server") {
    NetTest t;
    Client& c = t.addClient("alice");
    for (int i = 0; i < 200 && !c.active(); ++i) t.step(1);
    REQUIRE(c.active());
    CHECK(t.server.clientCount() == 1);
    CHECK(c.mapName() == "test");

    cmd::executeNow("+forward");
    t.step(60);
    cmd::executeNow("-forward");
    t.step(40);
    const auto& serverState = t.server.game()->playerState(0);
    CHECK(serverState.origin.x > 140.0f); // moved east from the spawn at x=128
    // After the input stops and everything is acknowledged, client and server agree.
    CHECK(glm::length(c.latestState().origin - serverState.origin) < 0.5f);
    CHECK(c.netGraph().predictionError < 2.0f);
}

TEST_CASE("multiplayer: two clients see each other") {
    NetTest t;
    Client& a = t.addClient("alice");
    Client& b = t.addClient("bob");
    for (int i = 0; i < 300 && !(a.active() && b.active()); ++i) t.step(1);
    REQUIRE(a.active());
    REQUIRE(b.active());
    CHECK(t.server.clientCount() == 2);
    t.step(20);
    auto seesPlayer = [](const Client& c) {
        std::vector<RenderEntity> ents;
        c.renderEntities(ents);
        for (auto& e : ents)
            if (e.model == game::kPlayerModel) return true;
        return false;
    };
    CHECK(seesPlayer(a));
    CHECK(seesPlayer(b));

    // Bob leaves; Alice stops seeing him.
    b.disconnect();
    t.step(30);
    CHECK(t.server.clientCount() == 1);
    CHECK_FALSE(seesPlayer(a));
}

TEST_CASE("multiplayer: the server rejects a client with the wrong password") {
    auto* pw = console::findVar("sv_password");
    REQUIRE(pw);
    pw->setValue("secret");
    NetTest t;
    Client& c = t.addClient("mallory");
    for (int i = 0; i < 100 && c.state() != Client::State::Disconnected; ++i) t.step(1);
    CHECK(c.state() == Client::State::Disconnected);
    CHECK(c.disconnectReason().find("password") != std::string::npos);
    pw->setValue("");
}

TEST_CASE("lag compensation: shots hit where the shooter saw the target") {
    game::GameWorld world{game::EngineServices{}};
    world.levelInit("test", std::make_shared<const bsp::BspData>(k::test::testRoom()));
    world.clientConnect(0, "shooter");
    world.clientConnect(1, "target");
    // Shooter and target on the line y = -120 (clear of the pillar at the room's center).
    world.setPlayerOrigin(0, {-200, -120, 0});
    world.setPlayerOrigin(1, {100, -120, 0});
    // Record a second of history with the target standing still in the line of fire...
    uint32_t tick = 0;
    for (int i = 0; i < 30; ++i) world.frame(1.0f / 66, ++tick);
    uint32_t sawTick = tick;
    // ...then it steps out of the way.
    world.setPlayerOrigin(1, {100, 150, 0});
    for (int i = 0; i < 6; ++i) world.frame(1.0f / 66, ++tick);

    auto* shooter = world.player(0);
    shooter->state.viewAngles = {0, 0, 0}; // looking east along y = -120
    CHECK(world.fireHitscan(*shooter, sawTick) == world.player(1)->index);  // rewound: hit
    CHECK(world.fireHitscan(*shooter, tick) != world.player(1)->index);     // as of now: miss
    console::findVar("sv_lagcompensation")->setValue("0");
    CHECK(world.fireHitscan(*shooter, sawTick) != world.player(1)->index);  // no lag comp: miss
    console::findVar("sv_lagcompensation")->setValue("1");
}
