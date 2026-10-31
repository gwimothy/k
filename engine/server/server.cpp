#include "server/server.h"

#include "core/cvar.h"
#include "core/log.h"

#include <cmath>
#include "shared/gamemovement.h"

namespace k {
namespace {

ConVar sv_tickrate("sv_tickrate", "66", FCVAR_NONE, "Server simulation rate (ticks/sec). Applied on map load.", 10.0f, 128.0f);
ConVar sv_cheats("sv_cheats", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Allow cheat commands and cvars.");
ConVar sv_maxspeed("sv_maxspeed", "320", FCVAR_REPLICATED | FCVAR_NOTIFY, "Maximum player speed.");
ConVar sv_friction("sv_friction", "4", FCVAR_REPLICATED | FCVAR_NOTIFY, "World friction.");
ConVar sv_stopspeed("sv_stopspeed", "100", FCVAR_REPLICATED | FCVAR_NOTIFY, "Minimum stopping speed on ground.");
ConVar sv_accelerate("sv_accelerate", "10", FCVAR_REPLICATED | FCVAR_NOTIFY, "Ground acceleration.");
ConVar sv_airaccelerate("sv_airaccelerate", "10", FCVAR_REPLICATED | FCVAR_NOTIFY, "Air acceleration.");
ConVar sv_gravity("sv_gravity", "800", FCVAR_REPLICATED | FCVAR_NOTIFY, "World gravity.");
ConVar sv_stepsize("sv_stepsize", "18", FCVAR_REPLICATED | FCVAR_NOTIFY, "Maximum step-up height.");
ConVar sv_autobunnyhopping("sv_autobunnyhopping", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "Holding jump re-jumps on landing.");
ConVar sv_noclipspeed("sv_noclipspeed", "5", FCVAR_ARCHIVE | FCVAR_NOTIFY, "Noclip speed multiplier.");
ConVar sv_noclipaccelerate("sv_noclipaccelerate", "5", FCVAR_ARCHIVE | FCVAR_NOTIFY, "Noclip acceleration.");

// sv_cheats gates FCVAR_CHEAT vars/commands everywhere.
struct CheatGateInstaller {
    CheatGateInstaller() {
        console::setCheatGate([] { return sv_cheats.getBool(); });
    }
} cheatGateInstaller;

} // namespace

bool Server::start(std::string mapName, std::shared_ptr<const bsp::BspData> map,
                   std::unique_ptr<net::NetChannel> clientChannel) {
    stop();
    mapName_ = std::move(mapName);
    map_ = std::move(map);
    channel_ = std::move(clientChannel);
    tickInterval_ = 1.0f / sv_tickrate.getFloat();
    tickCount_ = 0;
    lastCommandNumber_ = 0;
    clientConnected_ = false;
    spawnPlayer();
    active_ = true;
    log::info("Server started on '{}' at {} tick", mapName_, sv_tickrate.getInt());
    return true;
}

void Server::spawnPlayer() {
    player_ = {};
    spawnAngles_ = {};
    const bsp::Entity* spawn = map_ ? map_->findEntity("info_player_start") : nullptr;
    if (!spawn) {
        log::warn("Map has no info_player_start; spawning at the origin");
        return;
    }
    player_.origin = bsp::entityVec3(*spawn, "origin");
    Vec3 a = bsp::entityVec3(*spawn, "angles");
    spawnAngles_ = {a.x, a.y, a.z};
}

void Server::stop() {
    if (!active_) return;
    active_ = false;
    channel_.reset();
    map_.reset();
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
                    w.write(proto::Svc::SetAngle);
                    w.write(spawnAngles_);
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
                    handleStringCmd(r.readString());
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
    params.accelerate = sv_accelerate.getFloat();
    params.airAccelerate = sv_airaccelerate.getFloat();
    params.friction = sv_friction.getFloat();
    params.stopSpeed = sv_stopspeed.getFloat();
    params.gravity = sv_gravity.getFloat();
    params.stepSize = sv_stepsize.getFloat();
    params.jumpImpulse = std::sqrt(2.0f * params.gravity * 57.0f);
    params.autoBunnyhop = sv_autobunnyhopping.getBool();
    params.noclipSpeed = sv_noclipspeed.getFloat();
    params.noclipAccelerate = sv_noclipaccelerate.getFloat();
    playerMove(player_, cmd, tickInterval_, params, map_.get());
}

void Server::handleStringCmd(const std::string& text) {
    if (text == "noclip") {
        if (!sv_cheats.getBool()) {
            log::warn("Can't use noclip unless sv_cheats is 1");
            return;
        }
        player_.moveType = player_.moveType == MOVETYPE_NOCLIP ? MOVETYPE_WALK : MOVETYPE_NOCLIP;
        player_.velocity = Vec3(0);
        log::info("noclip {}", player_.moveType == MOVETYPE_NOCLIP ? "ON" : "OFF");
        return;
    }
    log::debug("Unknown client command '{}'", text);
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
