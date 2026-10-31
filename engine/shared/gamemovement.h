#pragma once
// Player movement, modelled on Source's CGameMovement (itself descended from Quake):
// ground/air acceleration, friction, gravity, jumping, crouching, stepping up stairs,
// sliding along walls, and noclip.
//
// It's pure: given a PlayerState, a UserCmd and the map, it produces the next PlayerState.
// The server runs it authoritatively; client-side prediction (Phase 7) will run the same code.

#include "shared/bspfile.h"
#include "shared/protocol.h"
#include "shared/usercmd.h"

namespace k {

enum PlayerFlags : uint32_t {
    FL_ONGROUND = 1u << 0,
    FL_DUCKING = 1u << 1,  // using the crouched hull
};

enum MoveType : uint32_t {
    MOVETYPE_WALK = 0,
    MOVETYPE_NOCLIP = 1,
};

struct MoveParams {
    float maxSpeed = 320.0f;
    float accelerate = 10.0f;
    float airAccelerate = 10.0f;
    float airSpeedCap = 30.0f;  // the wish-speed cap in the air that makes air strafing work
    float friction = 4.0f;
    float stopSpeed = 100.0f;
    float gravity = 800.0f;
    float stepSize = 18.0f;
    float jumpImpulse = 301.993377f; // sqrt(2 * 800 * 57): jumps 57 units at 800 gravity
    bool autoBunnyhop = false;
    float noclipSpeed = 5.0f;   // 5 = fly at 2x maxSpeed
    float noclipAccelerate = 5.0f;
};

// Player hulls (feet at origin) and eye heights, matching Source.
inline const Vec3 kHullMins{-16, -16, 0};
inline const Vec3 kHullMaxs{16, 16, 72};
inline const Vec3 kDuckHullMins{-16, -16, 0};
inline const Vec3 kDuckHullMaxs{16, 16, 36};
constexpr float kViewHeight = 64.0f;
constexpr float kDuckViewHeight = 28.0f;

constexpr uint32_t kPlayerMask = bsp::CONTENTS_SOLID | bsp::CONTENTS_SKY | bsp::CONTENTS_PLAYERCLIP;

// Advance the player by one UserCmd lasting `dt` seconds. `map` may be null (no collision).
void playerMove(proto::PlayerState& state, const UserCmd& cmd, float dt, const MoveParams& params,
                const bsp::BspData* map);

// Just the noclip flying part (also used when there's no map).
void playerMoveNoclip(proto::PlayerState& state, const UserCmd& cmd, float dt, const MoveParams& params);

} // namespace k
