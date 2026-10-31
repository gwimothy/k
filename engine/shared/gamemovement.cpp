#include "shared/gamemovement.h"

#include "shared/bsp_trace.h"

#include <algorithm>
#include <cmath>

namespace k {
namespace {

constexpr float kGroundNormalZ = 0.7f;   // steeper than ~45 degrees isn't ground
constexpr float kMaxGroundUpSpeed = 140.0f; // moving up faster than this means airborne
constexpr float kDuckTime = 0.2f;        // seconds for the eye height to settle

struct Mover {
    proto::PlayerState& s;
    const UserCmd& cmd;
    float dt;
    const MoveParams& p;
    const TraceWorld* world;

    bool onGround() const { return (s.flags & FL_ONGROUND) != 0; }
    bool ducking() const { return (s.flags & FL_DUCKING) != 0; }
    Vec3 mins() const { return ducking() ? kDuckHullMins : kHullMins; }
    Vec3 maxs() const { return ducking() ? kDuckHullMaxs : kHullMaxs; }

    bsp::TraceResult trace(const Vec3& from, const Vec3& to) const { return trace(from, to, mins(), maxs()); }
    bsp::TraceResult trace(const Vec3& from, const Vec3& to, const Vec3& mn, const Vec3& mx) const {
        return world->traceBox(from, to, mn, mx, kPlayerMask);
    }

    static void clipVelocity(const Vec3& in, const Vec3& normal, Vec3& out, float overbounce) {
        float backoff = glm::dot(in, normal) * overbounce;
        out = in - normal * backoff;
        // Stop tiny residual motion into the plane.
        float adjust = glm::dot(out, normal);
        if (adjust < 0.0f) out -= normal * adjust;
    }

    void categorizePosition() {
        if (s.velocity.z > kMaxGroundUpSpeed) {
            s.flags &= ~FL_ONGROUND;
            return;
        }
        auto tr = trace(s.origin, s.origin - Vec3(0, 0, 2));
        if (tr.fraction < 1.0f && tr.normal.z >= kGroundNormalZ && !tr.allSolid) {
            s.flags |= FL_ONGROUND;
            s.groundEntity = tr.entity;
            if (!tr.startSolid) s.origin = tr.endPos;
            if (s.velocity.z < 0) s.velocity.z = 0;
        } else {
            s.flags &= ~FL_ONGROUND;
            s.groundEntity = -1;
        }
    }

    void friction() {
        float speed = glm::length(s.velocity);
        if (speed < 0.1f) return;
        float control = std::max(speed, p.stopSpeed);
        float drop = control * p.friction * dt;
        float newSpeed = std::max(0.0f, speed - drop);
        s.velocity *= newSpeed / speed;
    }

    void accelerate(const Vec3& wishDir, float wishSpeed, float accel) {
        float current = glm::dot(s.velocity, wishDir);
        float add = wishSpeed - current;
        if (add <= 0) return;
        float speed = std::min(accel * dt * wishSpeed, add);
        s.velocity += wishDir * speed;
    }

    void airAccelerate(const Vec3& wishDir, float wishSpeed, float accel) {
        // The cap only limits how much speed you can add along wishDir, not your total
        // speed: turning while strafing keeps adding speed. That's air strafing.
        float capped = std::min(wishSpeed, p.airSpeedCap);
        float current = glm::dot(s.velocity, wishDir);
        float add = capped - current;
        if (add <= 0) return;
        float speed = std::min(accel * wishSpeed * dt, add);
        s.velocity += wishDir * speed;
    }

    // Slide along whatever we hit, up to four planes per move (Quake's PM_FlyMove).
    void tryPlayerMove() {
        constexpr int kMaxPlanes = 5;
        Vec3 planes[kMaxPlanes];
        int numPlanes = 0;
        Vec3 primal = s.velocity, original = s.velocity;
        float timeLeft = dt;
        for (int bump = 0; bump < 4; ++bump) {
            if (glm::dot(s.velocity, s.velocity) < 1e-6f) break;
            Vec3 end = s.origin + s.velocity * timeLeft;
            auto tr = trace(s.origin, end);
            if (tr.allSolid) {
                s.velocity = Vec3(0);
                return;
            }
            if (tr.fraction > 0) {
                s.origin = tr.endPos;
                original = s.velocity;
                numPlanes = 0;
            }
            if (tr.fraction == 1.0f) break;
            timeLeft -= timeLeft * tr.fraction;
            if (numPlanes >= kMaxPlanes) {
                s.velocity = Vec3(0);
                break;
            }
            planes[numPlanes++] = tr.normal;

            int i;
            for (i = 0; i < numPlanes; ++i) {
                clipVelocity(original, planes[i], s.velocity, 1.0f);
                int j;
                for (j = 0; j < numPlanes; ++j)
                    if (j != i && glm::dot(s.velocity, planes[j]) < 0) break;
                if (j == numPlanes) break;
            }
            if (i == numPlanes) {
                // Wedged between planes: slide along their crease or stop.
                if (numPlanes != 2) {
                    s.velocity = Vec3(0);
                    break;
                }
                Vec3 dir = glm::normalize(glm::cross(planes[0], planes[1]));
                s.velocity = dir * glm::dot(dir, s.velocity);
            }
            // Don't bounce back the way we came (avoids jitter in corners).
            if (glm::dot(s.velocity, primal) <= 0) {
                s.velocity = Vec3(0);
                break;
            }
        }
    }

    // Walking into something: also try stepping up onto it, and keep whichever went further.
    void stepMove() {
        Vec3 startPos = s.origin, startVel = s.velocity;

        tryPlayerMove();
        Vec3 downPos = s.origin, downVel = s.velocity;

        s.origin = startPos;
        s.velocity = startVel;
        auto up = trace(s.origin, s.origin + Vec3(0, 0, p.stepSize));
        if (!up.startSolid && !up.allSolid) s.origin = up.endPos;
        tryPlayerMove();
        auto down = trace(s.origin, s.origin - Vec3(0, 0, p.stepSize));
        if (!down.startSolid && !down.allSolid) s.origin = down.endPos;
        // Landed on something too steep, or didn't land at all: use the plain slide.
        if (down.fraction == 1.0f || down.normal.z < kGroundNormalZ) {
            s.origin = downPos;
            s.velocity = downVel;
            return;
        }
        Vec3 upPos = s.origin;
        auto flat = [](const Vec3& a, const Vec3& b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); };
        if (flat(downPos, startPos) > flat(upPos, startPos)) {
            s.origin = downPos;
            s.velocity = downVel;
        } else {
            s.velocity.z = downVel.z;
        }
    }

    // Keep feet on the ground when walking down stairs and slopes.
    void stayOnGround() {
        auto up = trace(s.origin, s.origin + Vec3(0, 0, 2));
        Vec3 start = up.endPos;
        auto tr = trace(start, s.origin - Vec3(0, 0, p.stepSize));
        if (tr.fraction > 0 && tr.fraction < 1 && !tr.startSolid && tr.normal.z >= kGroundNormalZ) {
            float dz = std::fabs(s.origin.z - tr.endPos.z);
            if (dz > 0.5f) s.origin = tr.endPos;
        }
    }

    void walkMove(const Vec3& wishDir, float wishSpeed) {
        s.velocity.z = 0;
        accelerate(wishDir, wishSpeed, p.accelerate);
        s.velocity.z = 0;
        float speed = glm::length(s.velocity);
        if (speed > p.maxSpeed * 4) s.velocity *= p.maxSpeed * 4 / speed; // sanity clamp
        if (speed < 1.0f) {
            s.velocity = Vec3(0);
            return;
        }
        Vec3 dest = s.origin + s.velocity * dt;
        auto tr = trace(s.origin, dest);
        if (tr.fraction == 1.0f) {
            s.origin = tr.endPos;
            stayOnGround();
            return;
        }
        stepMove();
        stayOnGround();
    }

    void airMove(const Vec3& wishDir, float wishSpeed) {
        airAccelerate(wishDir, wishSpeed, p.airAccelerate);
        tryPlayerMove();
    }

    void duck() {
        bool wantDuck = (cmd.buttons & IN_DUCK) != 0;
        float hullDelta = kHullMaxs.z - kDuckHullMaxs.z;
        if (wantDuck && !ducking()) {
            if (onGround()) {
                s.flags |= FL_DUCKING;
            } else {
                // Mid-air: pull the legs up, so the head stays where it was.
                auto tr = trace(s.origin, s.origin + Vec3(0, 0, hullDelta), kDuckHullMins, kDuckHullMaxs);
                s.origin = tr.endPos;
                s.flags |= FL_DUCKING;
            }
        } else if (!wantDuck && ducking()) {
            // Stand up only if there's room.
            Vec3 standOrigin = onGround() ? s.origin : s.origin - Vec3(0, 0, hullDelta);
            auto tr = trace(standOrigin, standOrigin, kHullMins, kHullMaxs);
            if (!tr.startSolid) {
                s.origin = standOrigin;
                s.flags &= ~FL_DUCKING;
            } else if (!onGround()) {
                auto tr2 = trace(s.origin, s.origin, kHullMins, kHullMaxs);
                if (!tr2.startSolid) s.flags &= ~FL_DUCKING;
            }
        }
        // Eye height eases toward its target.
        float target = ducking() ? kDuckViewHeight : kViewHeight;
        float rate = (kViewHeight - kDuckViewHeight) / kDuckTime * dt;
        if (s.viewHeight < target) s.viewHeight = std::min(target, s.viewHeight + rate);
        else s.viewHeight = std::max(target, s.viewHeight - rate);
        // An in-air duck or unduck moves the origin; keep the eyes steady.
        if (!onGround()) s.viewHeight = target;
    }

    void run() {
        s.viewAngles = cmd.viewAngles;
        duck();
        categorizePosition();

        bool jumpPressed = (cmd.buttons & IN_JUMP) && (p.autoBunnyhop || !(s.oldButtons & IN_JUMP));
        if (onGround()) {
            if (jumpPressed) {
                s.velocity.z = p.jumpImpulse;
                s.flags &= ~FL_ONGROUND;
            } else {
                s.velocity.z = 0;
                friction();
            }
        }

        // Wish direction from the view, flattened onto the ground plane.
        Vec3 forward, right;
        angleVectors({0, cmd.viewAngles.yaw, 0}, &forward, &right);
        Vec3 wishVel = forward * cmd.forwardMove + right * cmd.sideMove;
        wishVel.z = 0;
        float wishSpeed = glm::length(wishVel);
        Vec3 wishDir = wishSpeed > 0 ? wishVel / wishSpeed : Vec3(0);
        float maxSpeed = p.maxSpeed;
        if (ducking() && onGround()) maxSpeed *= 0.33333f;
        else if (cmd.buttons & IN_WALK) maxSpeed *= 0.52f;
        wishSpeed = std::min(wishSpeed, maxSpeed);

        if (onGround()) {
            walkMove(wishDir, wishSpeed);
        } else {
            s.velocity.z -= p.gravity * dt * 0.5f;
            airMove(wishDir, wishSpeed);
            s.velocity.z -= p.gravity * dt * 0.5f;
        }
        categorizePosition();
        s.oldButtons = cmd.buttons;
    }
};

} // namespace

void playerMoveNoclip(proto::PlayerState& s, const UserCmd& cmd, float dt, const MoveParams& p) {
    s.viewAngles = cmd.viewAngles;
    s.flags &= ~FL_ONGROUND;
    s.groundEntity = -1;

    float speed = glm::length(s.velocity);
    if (speed < 0.1f) {
        s.velocity = Vec3(0);
    } else {
        float control = std::max(speed, p.stopSpeed);
        float drop = control * p.friction * dt;
        s.velocity *= std::max(0.0f, speed - drop) / speed;
    }

    Vec3 forward, right, up;
    angleVectors(cmd.viewAngles, &forward, &right, &up);
    Vec3 wishVel = forward * cmd.forwardMove + right * cmd.sideMove;
    wishVel.z += cmd.upMove;

    float wishSpeed = glm::length(wishVel);
    Vec3 wishDir = wishSpeed > 0 ? wishVel / wishSpeed : Vec3(0);
    float speedScale = (cmd.buttons & IN_SPEED) ? 2.0f : 1.0f;
    float maxWish = p.maxSpeed * (p.noclipSpeed * 0.4f) * speedScale;
    wishSpeed = std::min(wishSpeed * (p.noclipSpeed * 0.4f) * speedScale, maxWish);

    float current = glm::dot(s.velocity, wishDir);
    float add = wishSpeed - current;
    if (add > 0) s.velocity += wishDir * std::min(add, p.noclipAccelerate * dt * wishSpeed * p.friction);
    s.origin += s.velocity * dt;
    s.oldButtons = cmd.buttons;
}

void playerMove(proto::PlayerState& s, const UserCmd& cmd, float dt, const MoveParams& p, const TraceWorld* world) {
    if (s.moveType == MOVETYPE_NOCLIP || !world) {
        playerMoveNoclip(s, cmd, dt, p);
        return;
    }
    Mover{s, cmd, dt, p, world}.run();
}

void playerMove(proto::PlayerState& s, const UserCmd& cmd, float dt, const MoveParams& p, const bsp::BspData* map) {
    if (!map) {
        playerMove(s, cmd, dt, p, static_cast<const TraceWorld*>(nullptr));
        return;
    }
    BspTraceWorld world(map);
    playerMove(s, cmd, dt, p, &world);
}

} // namespace k
