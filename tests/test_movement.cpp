#include "shared/bsp_trace.h"
#include "shared/gamemovement.h"
#include "test_room.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace k;
using k::test::testRoom;

namespace {

constexpr float kDt = 1.0f / 66.0f;
// Things come to rest 1/32 unit above surfaces (Source's DIST_EPSILON).
bool nearFloor(float z) { return std::fabs(z) < 0.05f; }

struct Sim {
    proto::PlayerState s;
    MoveParams params;
    const bsp::BspData* map = &testRoom();

    void run(int ticks, UserCmd cmd) {
        for (int i = 0; i < ticks; ++i) playerMove(s, cmd, kDt, params, map);
    }
    float horizontalSpeed() const { return glm::length(Vec2(s.velocity.x, s.velocity.y)); }
    bool onGround() const { return (s.flags & FL_ONGROUND) != 0; }
};

UserCmd forward(float yaw) {
    UserCmd c;
    c.viewAngles = {0, yaw, 0};
    c.forwardMove = 450;
    return c;
}

} // namespace

TEST_CASE("movement: falls and lands on the floor") {
    Sim sim;
    sim.s.origin = {128, 0, 40};
    sim.run(66, UserCmd{});
    CHECK(sim.onGround());
    CHECK(nearFloor(sim.s.origin.z));
    CHECK(sim.s.velocity.z == doctest::Approx(0.0f));
}

TEST_CASE("movement: walking reaches max speed and stops at walls") {
    Sim sim;
    sim.s.origin = {100, 0, 0};
    sim.run(20, forward(0)); // east
    CHECK(sim.horizontalSpeed() <= 320.5f);
    CHECK(sim.horizontalSpeed() > 280.0f);
    sim.run(200, forward(0));
    // East wall's inner face is at x=256; the hull is 16 wide each side.
    CHECK(sim.s.origin.x == doctest::Approx(240.0f).epsilon(0.002));
    CHECK(nearFloor(sim.s.origin.z));
    // Friction brings us to rest.
    sim.run(132, UserCmd{});
    CHECK(sim.horizontalSpeed() == doctest::Approx(0.0f));
}

TEST_CASE("movement: slides along a wall when moving diagonally into it") {
    Sim sim;
    sim.s.origin = {200, 0, 0};
    sim.run(132, forward(45)); // north-east: the east wall blocks x, y keeps going
    CHECK(sim.s.origin.x == doctest::Approx(240.0f).epsilon(0.002));
    CHECK(sim.s.origin.y > 150.0f);
}

TEST_CASE("movement: jump height matches Source (57 units)") {
    Sim sim;
    sim.s.origin = {128, 100, 0};
    sim.run(5, UserCmd{});
    REQUIRE(sim.onGround());
    UserCmd jump;
    jump.buttons = IN_JUMP;
    float peak = 0;
    for (int i = 0; i < 80; ++i) {
        playerMove(sim.s, jump, kDt, sim.params, sim.map);
        peak = std::max(peak, sim.s.origin.z);
    }
    CHECK(peak == doctest::Approx(57.0f).epsilon(0.05));
    // Holding jump doesn't re-jump on landing (no auto bunnyhop).
    CHECK(sim.onGround());
}

TEST_CASE("movement: steps up a 16-unit step but not a 64-unit block") {
    Sim sim;
    sim.s.origin = {120, -168, 0};
    sim.run(30, forward(0)); // east across the step at x 160..224
    bool climbed = sim.s.origin.z > 15.0f || sim.s.origin.x > 224.0f;
    CHECK(climbed);

    Sim blocked;
    blocked.s.origin = {-100, -168, 0};
    blocked.run(90, forward(180)); // west into the block at x -200..-136
    CHECK(nearFloor(blocked.s.origin.z));
    CHECK(blocked.s.origin.x == doctest::Approx(-120.0f).epsilon(0.002)); // block face at -136, minus 16
}

TEST_CASE("movement: crouching shrinks the hull, lowers the eyes and slows you down") {
    Sim sim;
    sim.s.origin = {128, 100, 0};
    UserCmd duck = forward(90);
    duck.buttons = IN_DUCK;
    sim.run(60, duck);
    CHECK((sim.s.flags & FL_DUCKING) != 0);
    CHECK(sim.s.viewHeight == doctest::Approx(kDuckViewHeight));
    CHECK(sim.horizontalSpeed() < 110.0f);
    sim.run(30, UserCmd{});
    CHECK((sim.s.flags & FL_DUCKING) == 0);
    CHECK(sim.s.viewHeight == doctest::Approx(kViewHeight));
}

TEST_CASE("movement: air strafing gains speed") {
    Sim sim;
    sim.s.origin = {-150, 50, 150};
    sim.s.velocity = {300, 0, 0};
    float yaw = 0;
    for (int i = 0; i < 25; ++i) {
        UserCmd c;
        yaw += 3.0f; // turn left...
        c.viewAngles = {0, yaw, 0};
        c.sideMove = -450; // ...while strafing left
        playerMove(sim.s, c, kDt, sim.params, sim.map);
    }
    CHECK_FALSE(sim.onGround());
    CHECK(sim.horizontalSpeed() > 320.0f);
}

TEST_CASE("movement: noclip flies through walls") {
    Sim sim;
    sim.s.origin = {200, 0, 0};
    sim.s.moveType = MOVETYPE_NOCLIP;
    sim.run(66, forward(0));
    CHECK(sim.s.origin.x > 300.0f);
}

TEST_CASE("box trace: hull against walls and the floor") {
    const auto& b = testRoom();
    auto tr = bsp::traceBox(b, {100, 0, 1}, {400, 0, 1}, kHullMins, kHullMaxs);
    CHECK(tr.fraction < 1.0f);
    CHECK(tr.endPos.x == doctest::Approx(240.0f).epsilon(0.002));
    CHECK(tr.normal.x == doctest::Approx(-1.0f));
    auto down = bsp::traceBox(b, {100, 0, 50}, {100, 0, -50}, kHullMins, kHullMaxs);
    CHECK(nearFloor(down.endPos.z));
    CHECK(down.normal.z == doctest::Approx(1.0f));
    auto stuck = bsp::traceBox(b, {0, 0, 10}, {10, 0, 10}, kHullMins, kHullMaxs); // inside the pillar
    CHECK(stuck.startSolid);
}
