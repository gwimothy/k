#pragma once
// Player movement shared by server (authoritative) and, later, client prediction.
// Phase 1 only has noclip flying; real walking/collision arrives in Phase 4.

#include "shared/protocol.h"
#include "shared/usercmd.h"

namespace k {

struct MoveParams {
    float maxSpeed = 320.0f;
    float noclipSpeed = 5.0f;      // 5 = fly at 2x maxSpeed
    float noclipAccelerate = 5.0f;
    float friction = 4.0f;
    float stopSpeed = 100.0f;
};

// Advance the player by one UserCmd lasting `dt` seconds.
void playerMoveNoclip(proto::PlayerState& state, const UserCmd& cmd, float dt, const MoveParams& params);

} // namespace k
