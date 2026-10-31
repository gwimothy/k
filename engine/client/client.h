#pragma once
// The game client: turns input into UserCmds, sends them to the server, receives snapshots,
// and produces the view to render.

#include "net/channel.h"
#include "shared/protocol.h"
#include "shared/usercmd.h"

#include <cstdint>
#include <memory>
#include <string>

namespace k {

struct ViewSetup {
    Vec3 origin{0};
    QAngle angles;
    float fov = 90.0f; // horizontal FOV at 4:3, like Source
};

class Client {
public:
    void connect(std::unique_ptr<net::NetChannel> channel, std::string playerName);
    void disconnect();
    bool connected() const { return channel_ != nullptr; }
    bool active() const { return gotServerInfo_ && snapshots_ > 0; }

    // Per rendered frame: apply mouse movement to the view angles.
    void frameMouse(float dx, float dy);
    // Per rendered frame: apply +left/+right/+lookup/+lookdown.
    void frameKeyboardLook(float dt);

    // Per server tick: build a UserCmd from current input and send it.
    void createMove(float tickInterval);

    // Drain messages from the server.
    void readPackets();

    // View for rendering. `alpha` in [0,1] is how far we are between the last two snapshots.
    ViewSetup view(float alpha) const;

    const proto::PlayerState& latestState() const { return current_; }
    void setViewAngles(const QAngle& a) { viewAngles_ = a; }
    const QAngle& viewAngles() const { return viewAngles_; }
    const std::string& mapName() const { return mapName_; }
    uint32_t serverTick() const { return serverTick_; }

private:
    std::unique_ptr<net::NetChannel> channel_;
    bool gotServerInfo_ = false;
    std::string mapName_;
    float serverTickInterval_ = 1.0f / 66.0f;

    QAngle viewAngles_;
    uint32_t commandNumber_ = 0;

    uint32_t snapshots_ = 0;
    uint32_t serverTick_ = 0;
    proto::PlayerState previous_, current_;
};

} // namespace k
