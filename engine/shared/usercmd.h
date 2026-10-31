#pragma once
// A UserCmd is one tick of player input, sent from client to server (Source's CUserCmd).

#include "net/bytebuffer.h"
#include "shared/kmath.h"

#include <cstdint>

namespace k {

enum InButtons : uint32_t {
    IN_ATTACK = 1u << 0,
    IN_JUMP = 1u << 1,
    IN_DUCK = 1u << 2,
    IN_FORWARD = 1u << 3,
    IN_BACK = 1u << 4,
    IN_USE = 1u << 5,
    IN_MOVELEFT = 1u << 6,
    IN_MOVERIGHT = 1u << 7,
    IN_ATTACK2 = 1u << 8,
    IN_RELOAD = 1u << 9,
    IN_SPEED = 1u << 10, // sprint
    IN_WALK = 1u << 11,
};

struct UserCmd {
    uint32_t commandNumber = 0;
    uint32_t tickCount = 0;
    QAngle viewAngles;
    float forwardMove = 0; // units/sec, + is forward
    float sideMove = 0;    // + is right
    float upMove = 0;      // + is up
    uint32_t buttons = 0;

    bool operator==(const UserCmd&) const = default;

    void write(net::ByteWriter& w) const {
        w.write(commandNumber);
        w.write(tickCount);
        w.write(viewAngles);
        w.write(forwardMove);
        w.write(sideMove);
        w.write(upMove);
        w.write(buttons);
    }

    static UserCmd read(net::ByteReader& r) {
        UserCmd c;
        c.commandNumber = r.read<uint32_t>();
        c.tickCount = r.read<uint32_t>();
        c.viewAngles = r.read<QAngle>();
        c.forwardMove = r.read<float>();
        c.sideMove = r.read<float>();
        c.upMove = r.read<float>();
        c.buttons = r.read<uint32_t>();
        return c;
    }
};

} // namespace k
