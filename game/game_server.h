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

// Services the engine provides to the game.
struct EngineServices {
    std::function<void(const std::string&)> print;        // console message to the client
    std::function<void(const std::string&)> centerPrint;  // on-screen message to the client
    // Play (or stop) a sound script on the client. entity > 0 attaches it to that entity.
    std::function<void(const std::string& name, int entity, const Vec3& origin, float volume, bool stop)> emitSound;
};

class IGameServer {
public:
    virtual ~IGameServer() = default;

    virtual bool levelInit(const std::string& mapName, std::shared_ptr<const bsp::BspData> map) = 0;
    virtual void levelShutdown() = 0;

    // One connected player (multiplayer comes in Phase 7).
    virtual void clientPutInServer(const std::string& name) = 0;
    virtual void runCommand(const UserCmd& cmd, float dt) = 0;
    virtual void clientCommand(const std::string& text) = 0;

    // After all commands for a tick: think functions, the I/O event queue, movers.
    virtual void frame(float dt) = 0;

    virtual const proto::PlayerState& playerState() const = 0;
    virtual QAngle spawnAngles() const = 0;
    virtual void setPlayerOrigin(const Vec3& origin) = 0;

    // Model table (index -> "*N" brush model or a model path) and entity states for snapshots.
    virtual const std::vector<std::string>& modelTable() const = 0;
    virtual void buildEntityStates(std::vector<proto::EntityState>& out) const = 0;

    virtual double time() const = 0;

    // True once after the game moved the player in a way that should snap the client's view
    // (teleporters). Fills in the angles to use.
    virtual bool takeAngleOverride(QAngle& angles) = 0;
};

std::unique_ptr<IGameServer> createGameServer(EngineServices services);

} // namespace k::game
