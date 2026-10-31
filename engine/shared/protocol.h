#pragma once
// Message ids for the client <-> server protocol.

#include "net/bytebuffer.h"
#include "shared/kmath.h"

#include <cstdint>

namespace k::proto {

constexpr uint32_t kProtocolVersion = 4;

// Client -> server
enum class Clc : uint8_t {
    Connect = 1, // u32 protocol version, string name
    Move = 2,    // u8 count, UserCmd[count]
    StringCmd = 3,
};

// Server -> client
enum class Svc : uint8_t {
    ServerInfo = 1,  // u32 protocol version, f32 tick interval, string map name, u16 n, string models[n]
    Snapshot = 2,    // u32 server tick, u32 last acked command, PlayerState, u16 n, EntityState[n]
    Print = 3,       // string
    SetAngle = 4,    // QAngle: force the client's view angles (spawn, teleport)
    CenterPrint = 5, // string shown in the middle of the screen
};

// State of the local player, sent every tick. Replaced by general entity snapshots in Phase 5.
struct PlayerState {
    Vec3 origin{0};
    Vec3 velocity{0};
    QAngle viewAngles;
    float viewHeight = 64.0f;
    uint32_t flags = 0;      // PlayerFlags
    uint32_t moveType = 0;   // MoveType
    uint32_t oldButtons = 0; // buttons from the previous command (jump needs a fresh press)
    int32_t groundEntity = -1; // entity stood on: 0 world, > 0 entity (server-side bookkeeping)

    void write(net::ByteWriter& w) const {
        w.write(origin);
        w.write(velocity);
        w.write(viewAngles);
        w.write(viewHeight);
        w.write(flags);
        w.write(moveType);
        w.write(oldButtons);
    }
    static PlayerState read(net::ByteReader& r) {
        PlayerState s;
        s.origin = r.read<Vec3>();
        s.velocity = r.read<Vec3>();
        s.viewAngles = r.read<QAngle>();
        s.viewHeight = r.read<float>();
        s.flags = r.read<uint32_t>();
        s.moveType = r.read<uint32_t>();
        s.oldButtons = r.read<uint32_t>();
        return s;
    }
};

// A networked entity as the client sees it. Full state every tick for now; Phase 7 adds
// delta compression.
struct EntityState {
    uint16_t index = 0;
    uint16_t model = 0;   // index into the ServerInfo model table, 0 = none
    Vec3 origin{0};
    QAngle angles;
    uint32_t effects = 0; // bit 0: don't draw

    void write(net::ByteWriter& w) const {
        w.write(index);
        w.write(model);
        w.write(origin);
        w.write(angles);
        w.write(effects);
    }
    static EntityState read(net::ByteReader& r) {
        EntityState s;
        s.index = r.read<uint16_t>();
        s.model = r.read<uint16_t>();
        s.origin = r.read<Vec3>();
        s.angles = r.read<QAngle>();
        s.effects = r.read<uint32_t>();
        return s;
    }
};

} // namespace k::proto
