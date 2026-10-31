#pragma once
// The engine side of the server: tick timing, the client connection, and snapshots.
// Game logic (entities, movement, rules) lives in the game library behind IGameServer.

#include "game_server.h"
#include "net/channel.h"
#include "shared/bspfile.h"
#include "shared/protocol.h"
#include "shared/usercmd.h"

#include <cstdint>
#include <memory>
#include <string>

namespace k {

class Server {
public:
    Server();
    ~Server();

    bool start(std::string mapName, std::shared_ptr<const bsp::BspData> map,
               std::unique_ptr<net::NetChannel> clientChannel);
    void stop();
    bool active() const { return active_; }

    // Run exactly one simulation tick: read client messages, simulate, send snapshots.
    void tick();

    float tickInterval() const { return tickInterval_; }
    uint32_t tickCount() const { return tickCount_; }
    const std::string& mapName() const { return mapName_; }
    const proto::PlayerState& player() const;
    const bsp::BspData* map() const { return map_.get(); }
    void setPlayerOrigin(const Vec3& origin);
    game::IGameServer* game() const { return game_.get(); }

private:
    void readMessages();
    void sendSnapshot();
    void sendReliable(const net::ByteWriter& w);

    bool active_ = false;
    std::string mapName_;
    std::shared_ptr<const bsp::BspData> map_;
    float tickInterval_ = 1.0f / 66.0f;
    uint32_t tickCount_ = 0;

    std::unique_ptr<net::NetChannel> channel_;
    std::unique_ptr<game::IGameServer> game_;
    bool clientConnected_ = false;
    uint32_t lastCommandNumber_ = 0;
};

} // namespace k
