#pragma once
// The game client: input -> UserCmds -> server; snapshots -> prediction + interpolation.
//
// * Local player: predicted. Every unacknowledged command is re-run on top of the latest
//   server state with the same movement code the server uses, so movement responds
//   instantly even with latency. Mispredictions are smoothed out over a few frames.
// * Everything else: interpolated cl_interp seconds in the past between snapshots, so it
//   moves smoothly even when packets are late or lost.

#include "net/channel.h"
#include "net/socket.h"
#include "net/udp_channel.h"
#include "shared/bspfile.h"
#include "shared/protocol.h"
#include "shared/usercmd.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace k {

// An entity ready to draw: interpolated position, resolved model name.
struct RenderEntity {
    int index;
    std::string model; // "*N" brush model or a model path
    Vec3 origin;
    QAngle angles;
};

struct SoundEvent {
    std::string name;
    int entity;
    Vec3 origin;
    float volume;
    bool stop;
};

struct TempEntityEvent {
    uint8_t type;
    Vec3 position, normal;
};

struct KillFeedEntry {
    std::string attacker, victim, how;
    double time;
};

struct ViewSetup {
    Vec3 origin{0};
    QAngle angles;
    float fov = 90.0f; // horizontal FOV at 4:3, like Source
};

class Client {
public:
    enum class State { Disconnected, Connecting, Connected, Active };

    void connectLocal(std::unique_ptr<net::NetChannel> channel, std::string playerName);
    bool connectRemote(const net::Address& server, std::string playerName, const std::string& password = {});
    void disconnect(const std::string& reason = {});
    State state() const { return state_; }
    bool connected() const { return state_ == State::Connected || state_ == State::Active; }
    bool active() const { return state_ == State::Active; }
    bool remote() const { return remote_; }

    // The map the server told us to load (set once per connection). The host loads it and
    // hands the BSP back with setMap().
    std::optional<std::string> takeMapToLoad();
    void setMap(std::shared_ptr<const bsp::BspData> map) { map_ = std::move(map); }

    // Per rendered frame: mouse/keyboard look.
    void frameMouse(float dx, float dy);
    void frameKeyboardLook(float dt);

    // Per rendered frame: advance the client clock and fade prediction errors.
    void advance(float frameTime);
    // Per client tick (the server's tick rate): build and send a UserCmd, run prediction.
    void tick(double now, float frameTime);
    // Network input; call every frame.
    void readPackets(double now);
    // Send a command string for the server to run (e.g. "noclip").
    void sendStringCmd(const std::string& text);

    // View for rendering. `alpha` in [0,1]: how far between the last two client ticks.
    ViewSetup view(float alpha) const;
    // Visible networked entities, interpolated.
    void renderEntities(std::vector<RenderEntity>& out) const;

    const proto::PlayerState& latestState() const { return predicted_; }
    const std::string& mapName() const { return mapName_; }
    uint32_t serverTick() const { return latestTick_; }
    void setViewAngles(const QAngle& a) { viewAngles_ = a; }
    const QAngle& viewAngles() const { return viewAngles_; }
    float tickInterval() const { return serverTickInterval_; }

    const std::string& centerMessage() const { return centerMessage_; }
    double centerMessageTime() const { return centerMessageTime_; }
    std::vector<SoundEvent> takeSounds() { return std::move(sounds_); }
    std::vector<TempEntityEvent> takeTempEntities() { return std::move(tempEntities_); }
    const std::deque<KillFeedEntry>& killFeed() const { return killFeed_; }
    double lastHitConfirm() const { return lastHitConfirm_; }
    const std::string& disconnectReason() const { return disconnectReason_; }

    struct NetGraph {
        net::NetStats net;
        float predictionError = 0; // units, last correction
        int snapshotsPerSec = 0;
        float interpMs = 0;
    };
    NetGraph netGraph() const;

private:
    struct Snapshot {
        uint32_t tick = 0;
        proto::PlayerState player;
        uint32_t lastCommand = 0;
        std::vector<proto::EntityState> entities;
        bool valid = false;
    };

    void handleMessage(net::ByteReader& r, double now);
    void readSnapshot(net::ByteReader& r, double now);
    void predict();
    const Snapshot* findSnapshot(uint32_t tick) const;
    void sendConnectPacket(double now);

    State state_ = State::Disconnected;
    bool remote_ = false;
    std::unique_ptr<net::NetChannel> channel_;
    net::UdpSocket socket_;
    net::Address serverAddress_;
    net::UdpChannel* udp_ = nullptr;
    std::string playerName_, password_;
    double nextConnectAttempt_ = 0, connectDeadline_ = 0;
    std::string disconnectReason_;

    std::string mapName_;
    bool mapPending_ = false;
    std::shared_ptr<const bsp::BspData> map_;
    float serverTickInterval_ = 1.0f / 66.0f;
    std::vector<std::string> modelTable_;
    int slot_ = -1;

    QAngle viewAngles_;
    uint32_t commandNumber_ = 0;
    std::deque<UserCmd> commands_; // sent, not yet acknowledged

    std::array<Snapshot, 32> snapshots_;
    uint32_t latestTick_ = 0;
    double clientTime_ = 0;     // our estimate of server time, advanced per frame
    bool haveClock_ = false;

    proto::PlayerState predicted_, previousPredicted_;
    Vec3 predictionError_{0};   // smoothed away over cl_smoothtime
    float lastErrorSize_ = 0;
    std::vector<std::pair<uint32_t, Vec3>> predictedHistory_; // command number -> predicted origin

    std::string centerMessage_;
    double centerMessageTime_ = -1e9;
    std::vector<SoundEvent> sounds_;
    std::vector<TempEntityEvent> tempEntities_;
    std::deque<KillFeedEntry> killFeed_;
    double lastHitConfirm_ = -1e9;
    int snapshotsThisSecond_ = 0, snapshotsPerSec_ = 0;
    double snapshotCounterStart_ = 0;
};

} // namespace k
