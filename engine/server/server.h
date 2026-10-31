#pragma once
// The engine side of the server: timing, client connections (a local loopback client and
// remote UDP clients), and delta-compressed snapshots. Game logic lives in the game library
// behind IGameServer.

#include "game_server.h"
#include "net/channel.h"
#include "net/socket.h"
#include "net/udp_channel.h"
#include "shared/bspfile.h"
#include "shared/protocol.h"
#include "shared/usercmd.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace k {

class Server {
public:
    Server();
    ~Server();

    // listenPort 0 = no network clients (single player only).
    bool start(std::string mapName, std::shared_ptr<const bsp::BspData> map, uint16_t listenPort);
    void stop();
    bool active() const { return active_; }

    // Adds the in-process client (listen server / single player). Returns its slot.
    int addLocalClient(std::unique_ptr<net::NetChannel> channel);

    // One simulation tick: network input, game frame, snapshots out.
    void tick(double now);

    float tickInterval() const { return tickInterval_; }
    uint32_t tickCount() const { return tickCount_; }
    const std::string& mapName() const { return mapName_; }
    const bsp::BspData* map() const { return map_.get(); }
    game::IGameServer* game() const { return game_.get(); }
    uint16_t listenPort() const { return socket_.isOpen() ? socket_.localPort() : 0; }
    int clientCount() const;
    void printStatus() const;

    // Convenience for the local player (slot of the loopback client, usually 0).
    int localSlot() const { return localSlot_; }
    const proto::PlayerState& player() const;
    void setPlayerOrigin(const Vec3& origin);

private:
    struct Snapshot {
        uint32_t tick = 0;
        std::vector<proto::EntityState> entities;
    };
    struct Client {
        bool active = false;
        bool local = false;
        bool spawned = false;  // got ServerInfo; receives snapshots
        std::unique_ptr<net::NetChannel> channel;
        net::UdpChannel* udp = nullptr; // same object as channel, for remote clients
        net::Address address;
        std::string name;
        uint32_t lastCommand = 0;
        uint32_t ackedTick = 0;
        std::array<Snapshot, 32> snapshots;
    };

    void pollSocket(double now);
    void handleConnectionless(const net::Address& from, const std::string& text, double now);
    void connectClient(int slot, const std::string& name);
    void dropClient(int slot, const std::string& reason);
    void readClientMessages(int slot);
    void sendServerInfo(int slot);
    void sendSnapshot(int slot);
    void broadcastReliable(const net::ByteWriter& w);
    void sendReliableTo(int slot, const net::ByteWriter& w);
    void replicateCvars();

    bool active_ = false;
    std::string mapName_;
    std::shared_ptr<const bsp::BspData> map_;
    float tickInterval_ = 1.0f / 66.0f;
    uint32_t tickCount_ = 0;
    std::array<Client, proto::kMaxClients> clients_;
    int localSlot_ = -1;
    net::UdpSocket socket_;
    std::unique_ptr<game::IGameServer> game_;
    std::map<std::string, std::string> sentCvars_;
    double now_ = 0;
};

} // namespace k
