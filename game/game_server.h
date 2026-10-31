#pragma once
// The interface between the engine's server and the game code (Source's IServerGameDLL).
//
// The engine owns networking, timing and the map file; the game owns entities and rules.
// The game is built as its own library (k_game) and only talks to the engine through this
// header and the shared/ code, so it can become a dynamically loaded module later.

#include "shared/bspfile.h"
#include "shared/protocol.h"
#include "shared/usercmd.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace k::game {

// Services the engine provides to the game. `slot` -1 means every client.
struct EngineServices {
    std::function<void(const std::string&)> print;        // console message to every client
    std::function<void(const std::string&)> centerPrint;  // on-screen message to every client
    std::function<void(int slot, const std::string&)> centerPrintTo;
    // Play (or stop) a sound script on the client. entity > 0 attaches it to that entity.
    std::function<void(const std::string& name, int entity, const Vec3& origin, float volume, bool stop)> emitSound;
};

class IGameServer {
public:
    virtual ~IGameServer() = default;

    virtual bool levelInit(const std::string& mapName, std::shared_ptr<const bsp::BspData> map) = 0;
    virtual void levelShutdown() = 0;

    // Players, by client slot (0 .. proto::kMaxClients-1).
    virtual void clientConnect(int slot, const std::string& name) = 0;
    virtual void clientDisconnect(int slot) = 0;
    virtual void runCommand(int slot, const UserCmd& cmd, float dt) = 0;
    virtual void clientCommand(int slot, const std::string& text) = 0;

    // After all commands for a tick: think functions, the I/O event queue, movers, physics.
    // `tick` is the server tick number (used to record history for lag compensation).
    virtual void frame(float dt, uint32_t tick) = 0;

    virtual const proto::PlayerState& playerState(int slot) const = 0;
    virtual QAngle spawnAngles(int slot) const = 0;
    virtual void setPlayerOrigin(int slot, const Vec3& origin) = 0;

    // Model table (index -> "*N" brush model or a model path) and what `slot` should see.
    virtual const std::vector<std::string>& modelTable() const = 0;
    virtual void buildEntityStates(int slot, std::vector<proto::EntityState>& out) const = 0;

    virtual double time() const = 0;

    // True once after the game moved a player in a way that should snap their view
    // (teleporters). Fills in the angles to use.
    virtual bool takeAngleOverride(int slot, QAngle& angles) = 0;
};

std::unique_ptr<IGameServer> createGameServer(EngineServices services);

} // namespace k::game
