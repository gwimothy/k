#include "shared/kmath.h"
#include "shared/gamemovement.h"

#include <doctest/doctest.h>

using namespace k;

TEST_CASE("angleVectors follows Source conventions") {
    Vec3 f, r, u;
    angleVectors({0, 0, 0}, &f, &r, &u);
    CHECK(f.x == doctest::Approx(1));
    CHECK(r.y == doctest::Approx(-1)); // right is -Y
    CHECK(u.z == doctest::Approx(1));

    angleVectors({0, 90, 0}, &f);
    CHECK(f.y == doctest::Approx(1));

    angleVectors({90, 0, 0}, &f); // positive pitch looks down
    CHECK(f.z == doctest::Approx(-1));
}

TEST_CASE("Angle normalization and interpolation take the short way round") {
    CHECK(normalizeAngle(270) == doctest::Approx(-90));
    CHECK(normalizeAngle(-190) == doctest::Approx(170));
    CHECK(normalizeAngle(180) == doctest::Approx(180));
    float mid = lerpAngle(170, -170, 0.5f);
    CHECK(std::fabs(normalizeAngle(mid)) == doctest::Approx(180));
}

TEST_CASE("Noclip movement accelerates toward wish direction and stops with friction") {
    proto::PlayerState s;
    UserCmd cmd;
    cmd.forwardMove = 450;
    MoveParams p;
    for (int i = 0; i < 66; ++i) playerMoveNoclip(s, cmd, 1.0f / 66.0f, p);
    CHECK(s.origin.x > 100.0f);
    CHECK(std::fabs(s.origin.y) < 0.01f);

    cmd.forwardMove = 0;
    for (int i = 0; i < 132; ++i) playerMoveNoclip(s, cmd, 1.0f / 66.0f, p);
    CHECK(glm::length(s.velocity) == doctest::Approx(0.0f));
}
