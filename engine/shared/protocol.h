#pragma once
// Message ids for the client <-> server protocol.

#include "net/bytebuffer.h"
#include "shared/kmath.h"

#include <cstdint>
#include <vector>

namespace k::proto {

constexpr uint32_t kProtocolVersion = 7;
constexpr uint16_t kDefaultPort = 27015;
constexpr int kMaxClients = 16;

// Client -> server
enum class Clc : uint8_t {
    Connect = 1,   // u32 protocol version, string name (loopback only; UDP uses the handshake)
    Move = 2,      // u32 last snapshot tick received, u8 count, UserCmd[count] (newest last)
    StringCmd = 3, // string
    Disconnect = 4,
};

// Server -> client
enum class Svc : uint8_t {
    ServerInfo = 1,  // u32 protocol version, f32 tick interval, string map name, u16 n, string models[n],
                     // u16 n, (string name, string value)[n] replicated cvars, u8 player slot
    Snapshot = 2,    // u32 tick, u32 delta-from tick (0 = full), u32 last command, PlayerState, entity delta
    Print = 3,       // string
    SetAngle = 4,    // QAngle: force the client's view angles (spawn, teleport)
    CenterPrint = 5, // string shown in the middle of the screen
    Sound = 6,       // string name, u16 entity, Vec3 origin, f32 volume, u8 flags (1 = stop)
    SetCvar = 7,     // string name, string value (replicated cvar changed)
    Disconnect = 8,  // string reason
    TempEntity = 9,  // u8 type, Vec3 position, Vec3 normal (effects: impacts, blood)
    KillFeed = 10,   // string attacker, string victim, string how
    HitConfirm = 11, // (empty) your shot hit a player
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
    int16_t health = 100;
    uint8_t dead = 0;

    void write(net::ByteWriter& w) const {
        w.write(origin);
        w.write(velocity);
        w.write(viewAngles);
        w.write(viewHeight);
        w.write(flags);
        w.write(moveType);
        w.write(oldButtons);
        w.write(health);
        w.write(dead);
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
        s.health = r.read<int16_t>();
        s.dead = r.read<uint8_t>();
        return s;
    }
};

enum class TempEntityType : uint8_t {
    Impact = 1, // bullet hitting the world: dust + a decal
    Blood = 2,  // hitting a player
    Spark = 3,  // hitting a prop or metal
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

// Entity lists sorted by index. The delta writes only entities that changed, appeared or
// disappeared relative to `base`, with a mask of which fields changed.
void writeEntityDelta(net::ByteWriter& w, const std::vector<EntityState>& base, const std::vector<EntityState>& current);
// Rebuilds the current list from `base` and the delta. Returns false on malformed data.
bool readEntityDelta(net::ByteReader& r, const std::vector<EntityState>& base, std::vector<EntityState>& out);

} // namespace k::proto
