#pragma once
// Message ids for the client <-> server protocol.

#include "net/bytebuffer.h"
#include "shared/kmath.h"

#include <cstdint>

namespace k::proto {

constexpr uint32_t kProtocolVersion = 2;

// Client -> server
enum class Clc : uint8_t {
    Connect = 1, // u32 protocol version, string name
    Move = 2,    // u8 count, UserCmd[count]
    StringCmd = 3,
};

// Server -> client
enum class Svc : uint8_t {
    ServerInfo = 1, // u32 protocol version, f32 tick interval, string map name
    Snapshot = 2,   // u32 server tick, u32 last acked command, PlayerState
    Print = 3,      // string
    SetAngle = 4,   // QAngle: force the client's view angles (spawn, teleport)
};

// State of the local player, sent every tick. Replaced by general entity snapshots in Phase 5.
struct PlayerState {
    Vec3 origin{0};
    Vec3 velocity{0};
    QAngle viewAngles;
    float viewHeight = 64.0f;

    void write(net::ByteWriter& w) const {
        w.write(origin);
        w.write(velocity);
        w.write(viewAngles);
        w.write(viewHeight);
    }
    static PlayerState read(net::ByteReader& r) {
        PlayerState s;
        s.origin = r.read<Vec3>();
        s.velocity = r.read<Vec3>();
        s.viewAngles = r.read<QAngle>();
        s.viewHeight = r.read<float>();
        return s;
    }
};

} // namespace k::proto
