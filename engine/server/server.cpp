#include "server/server.h"

#include "core/cvar.h"
#include "core/log.h"
#include "shared/movement.h"

namespace k {
namespace {

ConVar sv_tickrate("sv_tickrate", "66", FCVAR_NONE, "Server simulation rate (ticks/sec). Applied on map load.", 10.0f, 128.0f);
ConVar sv_cheats("sv_cheats", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Allow cheat commands and cvars.");
ConVar sv_maxspeed("sv_maxspeed", "320", FCVAR_REPLICATED | FCVAR_NOTIFY, "Maximum player speed.");
ConVar sv_friction("sv_friction", "4", FCVAR_REPLICATED | FCVAR_NOTIFY, "World friction.");
ConVar sv_stopspeed("sv_stopspeed", "100", FCVAR_REPLICATED | FCVAR_NOTIFY, "Minimum stopping speed on ground.");
ConVar sv_noclipspeed("sv_noclipspeed", "5", FCVAR_ARCHIVE | FCVAR_NOTIFY, "Noclip speed multiplier.");
ConVar sv_noclipaccelerate("sv_noclipaccelerate", "5", FCVAR_ARCHIVE | FCVAR_NOTIFY, "Noclip acceleration.");

// sv_cheats gates FCVAR_CHEAT vars/commands everywhere.
struct CheatGateInstaller {
    CheatGateInstaller() {
        console::setCheatGate([] { return sv_cheats.getBool(); });
    }
} cheatGateInstaller;

} // namespace

bool Server::start(std::string mapName, std::unique_ptr<net::NetChannel> clientChannel) {
    stop();
    mapName_ = std::move(mapName);
    channel_ = std::move(clientChannel);
    tickInterval_ = 1.0f / sv_tickrate.getFloat();
    tickCount_ = 0;
    lastCommandNumber_ = 0;
    clientConnected_ = false;
    player_ = {};
    player_.origin = Vec3(0, 0, 0);
    active_ = true;
    log::info("Server started on '{}' at {} tick", mapName_, sv_tickrate.getInt());
    return true;
}

void Server::stop() {
    if (!active_) return;
    active_ = false;
    channel_.reset();
    log::info("Server shut down");
}

void Server::tick() {
    if (!active_) return;
    readMessages();
    ++tickCount_;
    if (clientConnected_) sendSnapshot();
}

void Server::readMessages() {
    while (auto msg = channel_->receive()) {
        net::ByteReader r(*msg);
        while (!r.atEnd() && !r.overflowed()) {
            auto type = static_cast<proto::Clc>(r.read<uint8_t>());
            switch (type) {
                case proto::Clc::Connect: {
                    auto version = r.read<uint32_t>();
                    auto name = r.readString();
                    if (version != proto::kProtocolVersion) {
                        log::warn("Client '{}' has protocol {}, server has {}", name, version, proto::kProtocolVersion);
                        return;
                    }
                    clientConnected_ = true;
                    log::info("{} connected", name);
                    net::ByteWriter w;
                    w.write(proto::Svc::ServerInfo);
                    w.write(proto::kProtocolVersion);
                    w.write(tickInterval_);
                    w.writeString(mapName_);
                    channel_->send(w.data());
                    break;
                }
                case proto::Clc::Move: {
                    auto count = r.read<uint8_t>();
                    for (int i = 0; i < count && !r.overflowed(); ++i) {
                        auto cmd = UserCmd::read(r);
                        // Commands can arrive more than once once we add redundancy over UDP.
                        if (cmd.commandNumber > lastCommandNumber_) processUserCmd(cmd);
                    }
                    break;
                }
                case proto::Clc::StringCmd: {
                    log::debug("client stringcmd: {}", r.readString());
                    break;
                }
                default:
                    log::warn("Server: bad client message {}", static_cast<int>(type));
                    return;
            }
        }
    }
}

void Server::processUserCmd(const UserCmd& cmd) {
    lastCommandNumber_ = cmd.commandNumber;
    MoveParams params;
    params.maxSpeed = sv_maxspeed.getFloat();
    params.friction = sv_friction.getFloat();
    params.stopSpeed = sv_stopspeed.getFloat();
    params.noclipSpeed = sv_noclipspeed.getFloat();
    params.noclipAccelerate = sv_noclipaccelerate.getFloat();
    // Noclip flying is all we have until world collision exists (Phase 4).
    playerMoveNoclip(player_, cmd, tickInterval_, params);
}

void Server::sendSnapshot() {
    net::ByteWriter w;
    w.write(proto::Svc::Snapshot);
    w.write(tickCount_);
    w.write(lastCommandNumber_);
    player_.write(w);
    channel_->send(w.data());
}

} // namespace k
