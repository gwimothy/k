#pragma once
// The game server. Always authoritative, even in single-player (local loopback client).

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
    bool start(std::string mapName, std::shared_ptr<const bsp::BspData> map,
               std::unique_ptr<net::NetChannel> clientChannel);
    void stop();
    bool active() const { return active_; }

    // Run exactly one simulation tick: read client messages, simulate, send snapshots.
    void tick();

    float tickInterval() const { return tickInterval_; }
    uint32_t tickCount() const { return tickCount_; }
    const std::string& mapName() const { return mapName_; }
    const proto::PlayerState& player() const { return player_; }
    const bsp::BspData* map() const { return map_.get(); }
    void setPlayerOrigin(const Vec3& origin) {
        player_.origin = origin;
        player_.velocity = Vec3(0);
    }

private:
    void readMessages();
    void processUserCmd(const UserCmd& cmd);
    void sendSnapshot();
    void spawnPlayer();
    void handleStringCmd(const std::string& text);

    bool active_ = false;
    std::string mapName_;
    std::shared_ptr<const bsp::BspData> map_;
    QAngle spawnAngles_;
    float tickInterval_ = 1.0f / 66.0f;
    uint32_t tickCount_ = 0;

    std::unique_ptr<net::NetChannel> channel_;
    bool clientConnected_ = false;
    uint32_t lastCommandNumber_ = 0;
    proto::PlayerState player_;
};

} // namespace k
