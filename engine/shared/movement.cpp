#include "shared/movement.h"

#include <algorithm>

namespace k {

void playerMoveNoclip(proto::PlayerState& s, const UserCmd& cmd, float dt, const MoveParams& p) {
    s.viewAngles = cmd.viewAngles;

    // Friction (Source's noclip uses the same formula as ground friction).
    float speed = glm::length(s.velocity);
    if (speed < 0.1f) {
        s.velocity = Vec3(0);
    } else {
        float control = std::max(speed, p.stopSpeed);
        float drop = control * p.friction * dt;
        float newSpeed = std::max(0.0f, speed - drop);
        s.velocity *= newSpeed / speed;
    }

    Vec3 forward, right, up;
    angleVectors(cmd.viewAngles, &forward, &right, &up);
    Vec3 wishVel = forward * cmd.forwardMove + right * cmd.sideMove;
    wishVel.z += cmd.upMove;

    float wishSpeed = glm::length(wishVel);
    Vec3 wishDir = wishSpeed > 0 ? wishVel / wishSpeed : Vec3(0);
    float speedScale = (cmd.buttons & IN_SPEED) ? 2.0f : 1.0f;
    // sv_noclipspeed 5 (the default) flies at 2x walking speed.
    float maxWish = p.maxSpeed * (p.noclipSpeed * 0.4f) * speedScale;
    wishSpeed = std::min(wishSpeed * (p.noclipSpeed * 0.4f) * speedScale, maxWish);

    // Accelerate toward wish velocity.
    float current = glm::dot(s.velocity, wishDir);
    float add = wishSpeed - current;
    if (add > 0) {
        float accel = std::min(add, p.noclipAccelerate * dt * wishSpeed * p.friction);
        s.velocity += wishDir * accel;
    }

    s.origin += s.velocity * dt;
}

} // namespace k
