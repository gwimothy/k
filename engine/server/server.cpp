#include "server/server.h"

#include "core/cvar.h"
#include "core/log.h"

namespace k {
namespace {

ConVar sv_tickrate("sv_tickrate", "66", FCVAR_NONE, "Server simulation rate (ticks/sec). Applied on map load.", 10.0f, 128.0f);
ConVar sv_cheats("sv_cheats", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Allow cheat commands and cvars.");

// sv_cheats gates FCVAR_CHEAT vars/commands everywhere.
struct CheatGateInstaller {
    CheatGateInstaller() {
        console::setCheatGate([] { return sv_cheats.getBool(); });
    }
} cheatGateInstaller;

} // namespace

Server::Server() = default;
Server::~Server() { stop(); }

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

    game::EngineServices services;
    services.print = [this](const std::string& text) {
        net::ByteWriter w;
        w.write(proto::Svc::Print);
        w.writeString(text);
        sendReliable(w);
    };
    services.centerPrint = [this](const std::string& text) {
        net::ByteWriter w;
        w.write(proto::Svc::CenterPrint);
        w.writeString(text);
        sendReliable(w);
    };
    game_ = game::createGameServer(std::move(services));
    if (!game_->levelInit(mapName_, map_)) {
        log::error("Game failed to load '{}'", mapName_);
        game_.reset();
        return false;
    }
    active_ = true;
    log::info("Server started on '{}' at {} tick", mapName_, sv_tickrate.getInt());
    return true;
}

void Server::stop() {
    if (!active_) return;
    active_ = false;
    if (game_) game_->levelShutdown();
    game_.reset();
    channel_.reset();
    map_.reset();
    log::info("Server shut down");
}

const proto::PlayerState& Server::player() const {
    static const proto::PlayerState none;
    return game_ ? game_->playerState() : none;
}

void Server::setPlayerOrigin(const Vec3& origin) {
    if (game_) game_->setPlayerOrigin(origin);
}

void Server::sendReliable(const net::ByteWriter& w) {
    // Loopback never drops; the UDP channel (Phase 7) will make this truly reliable.
    if (channel_ && clientConnected_) channel_->send(w.data());
}

void Server::tick() {
    if (!active_) return;
    readMessages();
    if (game_) game_->frame(tickInterval_);
    QAngle forced;
    if (game_ && game_->takeAngleOverride(forced)) {
        net::ByteWriter w;
        w.write(proto::Svc::SetAngle);
        w.write(forced);
        sendReliable(w);
    }
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
                    game_->clientPutInServer(name);
                    log::info("{} connected", name);
                    net::ByteWriter w;
                    w.write(proto::Svc::ServerInfo);
                    w.write(proto::kProtocolVersion);
                    w.write(tickInterval_);
                    w.writeString(mapName_);
                    const auto& models = game_->modelTable();
                    w.write(static_cast<uint16_t>(models.size()));
                    for (auto& m : models) w.writeString(m);
                    w.write(proto::Svc::SetAngle);
                    w.write(game_->spawnAngles());
                    channel_->send(w.data());
                    break;
                }
                case proto::Clc::Move: {
                    auto count = r.read<uint8_t>();
                    for (int i = 0; i < count && !r.overflowed(); ++i) {
                        auto cmd = UserCmd::read(r);
                        // Commands can arrive more than once once we add redundancy over UDP.
                        if (cmd.commandNumber <= lastCommandNumber_) continue;
                        lastCommandNumber_ = cmd.commandNumber;
                        game_->runCommand(cmd, tickInterval_);
                    }
                    break;
                }
                case proto::Clc::StringCmd: game_->clientCommand(r.readString()); break;
                default:
                    log::warn("Server: bad client message {}", static_cast<int>(type));
                    return;
            }
        }
    }
}

void Server::sendSnapshot() {
    net::ByteWriter w;
    w.write(proto::Svc::Snapshot);
    w.write(tickCount_);
    w.write(lastCommandNumber_);
    game_->playerState().write(w);
    static thread_local std::vector<proto::EntityState> states;
    game_->buildEntityStates(states);
    w.write(static_cast<uint16_t>(states.size()));
    for (auto& s : states) s.write(w);
    channel_->send(w.data());
}

} // namespace k
