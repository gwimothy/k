#include "server/server.h"

#include "core/cvar.h"
#include "core/log.h"

#include <algorithm>

namespace k {
namespace {

ConVar sv_tickrate("sv_tickrate", "66", FCVAR_NONE, "Server simulation rate (ticks/sec). Applied on map load.", 10.0f, 128.0f);
ConVar sv_cheats("sv_cheats", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Allow cheat commands and cvars.");
ConVar sv_maxplayers("sv_maxplayers", "8", FCVAR_NONE, "Maximum connected players.", 1.0f, static_cast<float>(proto::kMaxClients));
ConVar sv_password("sv_password", "", FCVAR_NONE, "Password required to join (empty = none).");

// sv_cheats gates FCVAR_CHEAT vars/commands everywhere.
struct CheatGateInstaller {
    CheatGateInstaller() {
        console::setCheatGate([] { return sv_cheats.getBool(); });
    }
} cheatGateInstaller;

std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        if (i >= s.size()) break;
        if (s[i] == '"') {
            size_t end = s.find('"', i + 1);
            out.push_back(s.substr(i + 1, end == std::string::npos ? std::string::npos : end - i - 1));
            i = end == std::string::npos ? s.size() : end + 1;
        } else {
            size_t end = s.find(' ', i);
            out.push_back(s.substr(i, end == std::string::npos ? std::string::npos : end - i));
            i = end == std::string::npos ? s.size() : end;
        }
    }
    return out;
}

} // namespace

Server::Server() = default;
Server::~Server() { stop(); }

bool Server::start(std::string mapName, std::shared_ptr<const bsp::BspData> map, uint16_t listenPort) {
    stop();
    mapName_ = std::move(mapName);
    map_ = std::move(map);
    tickInterval_ = 1.0f / sv_tickrate.getFloat();
    tickCount_ = 0;
    sentCvars_.clear();

    game::EngineServices services;
    services.print = [this](const std::string& text) {
        net::ByteWriter w;
        w.write(proto::Svc::Print);
        w.writeString(text);
        broadcastReliable(w);
    };
    services.centerPrint = [this](const std::string& text) {
        net::ByteWriter w;
        w.write(proto::Svc::CenterPrint);
        w.writeString(text);
        broadcastReliable(w);
    };
    services.centerPrintTo = [this](int slot, const std::string& text) {
        net::ByteWriter w;
        w.write(proto::Svc::CenterPrint);
        w.writeString(text);
        sendReliableTo(slot, w);
    };
    services.emitSound = [this](const std::string& name, int entity, const Vec3& origin, float volume, bool stop) {
        net::ByteWriter w;
        w.write(proto::Svc::Sound);
        w.writeString(name);
        w.write(static_cast<uint16_t>(std::max(0, entity)));
        w.write(origin);
        w.write(volume);
        w.write(static_cast<uint8_t>(stop ? 1 : 0));
        broadcastReliable(w);
    };
    services.tempEntity = [this](uint8_t type, const Vec3& pos, const Vec3& normal) {
        net::ByteWriter w;
        w.write(proto::Svc::TempEntity);
        w.write(type);
        w.write(pos);
        w.write(normal);
        broadcastReliable(w);
    };
    services.killFeed = [this](const std::string& attacker, const std::string& victim, const std::string& how) {
        net::ByteWriter w;
        w.write(proto::Svc::KillFeed);
        w.writeString(attacker);
        w.writeString(victim);
        w.writeString(how);
        broadcastReliable(w);
    };
    services.hitConfirm = [this](int slot) {
        net::ByteWriter w;
        w.write(proto::Svc::HitConfirm);
        sendReliableTo(slot, w);
    };
    game_ = game::createGameServer(std::move(services));
    if (!game_->levelInit(mapName_, map_)) {
        log::error("Game failed to load '{}'", mapName_);
        game_.reset();
        return false;
    }
    if (listenPort) {
        if (socket_.open(listenPort)) log::info("Listening for players on UDP port {}", socket_.localPort());
        else log::warn("Couldn't listen on port {}; only the local player can join", listenPort);
    }
    active_ = true;
    log::info("Server started on '{}' at {} tick", mapName_, sv_tickrate.getInt());
    return true;
}

void Server::stop() {
    if (!active_) return;
    for (int i = 0; i < proto::kMaxClients; ++i)
        if (clients_[static_cast<size_t>(i)].active) dropClient(i, "Server shutting down");
    active_ = false;
    if (game_) game_->levelShutdown();
    game_.reset();
    socket_.close();
    map_.reset();
    localSlot_ = -1;
    log::info("Server shut down");
}

int Server::clientCount() const {
    int n = 0;
    for (auto& c : clients_) n += c.active;
    return n;
}

int Server::addLocalClient(std::unique_ptr<net::NetChannel> channel) {
    for (int i = 0; i < proto::kMaxClients; ++i) {
        Client& c = clients_[static_cast<size_t>(i)];
        if (c.active) continue;
        c = Client{};
        c.active = true;
        c.local = true;
        c.channel = std::move(channel);
        localSlot_ = i;
        return i; // the client sends Clc::Connect with its name next
    }
    return -1;
}

const proto::PlayerState& Server::player() const {
    static const proto::PlayerState none;
    return game_ && localSlot_ >= 0 ? game_->playerState(localSlot_) : none;
}

void Server::setPlayerOrigin(const Vec3& origin) {
    if (game_ && localSlot_ >= 0) game_->setPlayerOrigin(localSlot_, origin);
}

void Server::sendReliableTo(int slot, const net::ByteWriter& w) {
    if (slot < 0 || slot >= proto::kMaxClients) return;
    Client& c = clients_[static_cast<size_t>(slot)];
    if (c.active && c.spawned && c.channel) c.channel->sendReliable(w.data());
}

void Server::broadcastReliable(const net::ByteWriter& w) {
    for (int i = 0; i < proto::kMaxClients; ++i) sendReliableTo(i, w);
}

void Server::connectClient(int slot, const std::string& name) {
    Client& c = clients_[static_cast<size_t>(slot)];
    c.name = name.empty() ? "player" : name.substr(0, 32);
    game_->clientConnect(slot, c.name);
    sendServerInfo(slot);
    c.spawned = true;
    log::info("{} connected{}", c.name, c.local ? "" : " from " + c.address.toString());
}

void Server::dropClient(int slot, const std::string& reason) {
    Client& c = clients_[static_cast<size_t>(slot)];
    if (!c.active) return;
    if (c.channel) {
        net::ByteWriter w;
        w.write(proto::Svc::Disconnect);
        w.writeString(reason);
        c.channel->sendReliable(w.data());
        c.channel->transmit(now_);
        c.channel->transmit(now_); // twice: no retries after this
    }
    if (game_) game_->clientDisconnect(slot);
    log::info("{} left ({})", c.name.empty() ? "client" : c.name, reason);
    c = Client{};
    if (slot == localSlot_) localSlot_ = -1;
}

void Server::sendServerInfo(int slot) {
    Client& c = clients_[static_cast<size_t>(slot)];
    net::ByteWriter w;
    w.write(proto::Svc::ServerInfo);
    w.write(proto::kProtocolVersion);
    w.write(tickInterval_);
    w.writeString(mapName_);
    const auto& models = game_->modelTable();
    w.write(static_cast<uint16_t>(models.size()));
    for (auto& m : models) w.writeString(m);
    std::vector<std::pair<std::string, std::string>> cvars;
    console::forEach([&](ConCommandBase& b) {
        if (!b.isCommand() && b.hasFlag(FCVAR_REPLICATED))
            cvars.emplace_back(b.name(), static_cast<ConVar&>(b).getString());
    });
    w.write(static_cast<uint16_t>(cvars.size()));
    for (auto& [n, v] : cvars) {
        w.writeString(n);
        w.writeString(v);
    }
    w.write(static_cast<uint8_t>(slot));
    w.write(proto::Svc::SetAngle);
    w.write(game_->spawnAngles(slot));
    c.channel->sendReliable(w.data());
}

void Server::replicateCvars() {
    console::forEach([&](ConCommandBase& b) {
        if (b.isCommand() || !b.hasFlag(FCVAR_REPLICATED)) return;
        const std::string& v = static_cast<ConVar&>(b).getString();
        auto it = sentCvars_.find(b.name());
        if (it != sentCvars_.end() && it->second == v) return;
        bool first = it == sentCvars_.end();
        sentCvars_[b.name()] = v;
        if (first) return; // initial values go out in ServerInfo
        net::ByteWriter w;
        w.write(proto::Svc::SetCvar);
        w.writeString(b.name());
        w.writeString(v);
        broadcastReliable(w);
    });
}

void Server::pollSocket(double now) {
    if (!socket_.isOpen()) return;
    net::Address from;
    while (auto packet = socket_.receiveFrom(from)) {
        if (auto text = net::readConnectionless(*packet)) {
            handleConnectionless(from, *text, now);
            continue;
        }
        for (auto& c : clients_)
            if (c.active && !c.local && c.address == from && c.udp) {
                c.udp->processPacket(*packet, now);
                break;
            }
    }
}

void Server::handleConnectionless(const net::Address& from, const std::string& text, double now) {
    auto words = splitWords(text);
    if (words.empty()) return;
    if (words[0] == "info") {
        net::sendConnectionless(socket_, from, "info \"" + mapName_ + "\" " + std::to_string(clientCount()) + " " +
                                                   std::to_string(sv_maxplayers.getInt()));
        return;
    }
    if (words[0] != "connect" || words.size() < 3) return;
    uint32_t version = static_cast<uint32_t>(std::strtoul(words[1].c_str(), nullptr, 10));
    if (version != proto::kProtocolVersion) {
        net::sendConnectionless(socket_, from, "reject \"Server uses protocol " + std::to_string(proto::kProtocolVersion) + "\"");
        return;
    }
    if (!sv_password.getString().empty() && (words.size() < 4 || words[3] != sv_password.getString())) {
        net::sendConnectionless(socket_, from, "reject \"Bad password\"");
        return;
    }
    // A resent connect from a client we already accepted: just re-accept.
    for (int i = 0; i < proto::kMaxClients; ++i) {
        Client& c = clients_[static_cast<size_t>(i)];
        if (c.active && !c.local && c.address == from) {
            net::sendConnectionless(socket_, from, "accept " + std::to_string(i));
            return;
        }
    }
    if (clientCount() >= sv_maxplayers.getInt()) {
        net::sendConnectionless(socket_, from, "reject \"Server is full\"");
        return;
    }
    for (int i = 0; i < proto::kMaxClients; ++i) {
        Client& c = clients_[static_cast<size_t>(i)];
        if (c.active) continue;
        c = Client{};
        c.active = true;
        c.address = from;
        auto chan = std::make_unique<net::UdpChannel>(
            [this, from](std::span<const uint8_t> data) { socket_.sendTo(from, data); }, now);
        c.udp = chan.get();
        c.channel = std::move(chan);
        net::sendConnectionless(socket_, from, "accept " + std::to_string(i));
        connectClient(i, words[2]);
        return;
    }
}

void Server::tick(double now) {
    if (!active_) return;
    now_ = now;
    pollSocket(now);
    for (int i = 0; i < proto::kMaxClients; ++i) {
        Client& c = clients_[static_cast<size_t>(i)];
        if (!c.active) continue;
        if (c.channel->timedOut(now)) {
            dropClient(i, "Timed out");
            continue;
        }
        readClientMessages(i);
    }
    ++tickCount_;
    game_->frame(tickInterval_, tickCount_);
    replicateCvars();
    for (int i = 0; i < proto::kMaxClients; ++i) {
        Client& c = clients_[static_cast<size_t>(i)];
        if (!c.active || !c.spawned) continue;
        QAngle forced;
        if (game_->takeAngleOverride(i, forced)) {
            net::ByteWriter w;
            w.write(proto::Svc::SetAngle);
            w.write(forced);
            c.channel->sendReliable(w.data());
        }
        sendSnapshot(i);
        c.channel->transmit(now);
    }
}

void Server::readClientMessages(int slot) {
    Client& c = clients_[static_cast<size_t>(slot)];
    while (c.active && c.channel) {
        auto msg = c.channel->receive();
        if (!msg) break;
        net::ByteReader r(*msg);
        while (!r.atEnd() && !r.overflowed()) {
            auto type = static_cast<proto::Clc>(r.read<uint8_t>());
            switch (type) {
                case proto::Clc::Connect: { // loopback only
                    auto version = r.read<uint32_t>();
                    auto name = r.readString();
                    if (version != proto::kProtocolVersion) {
                        dropClient(slot, "Protocol mismatch");
                        return;
                    }
                    if (!c.spawned) connectClient(slot, name);
                    break;
                }
                case proto::Clc::Move: {
                    uint32_t ack = r.read<uint32_t>();
                    if (ack > c.ackedTick && ack <= tickCount_) c.ackedTick = ack;
                    auto count = r.read<uint8_t>();
                    for (int i = 0; i < count && !r.overflowed(); ++i) {
                        auto cmd = UserCmd::read(r);
                        // Commands are sent redundantly (UDP may drop packets): run each once.
                        if (cmd.commandNumber <= c.lastCommand) continue;
                        c.lastCommand = cmd.commandNumber;
                        if (c.spawned) game_->runCommand(slot, cmd, tickInterval_);
                    }
                    break;
                }
                case proto::Clc::StringCmd: game_->clientCommand(slot, r.readString()); break;
                case proto::Clc::Disconnect: dropClient(slot, "Disconnected"); return;
                default:
                    dropClient(slot, "Bad message");
                    return;
            }
        }
    }
}

void Server::sendSnapshot(int slot) {
    Client& c = clients_[static_cast<size_t>(slot)];
    Snapshot& snap = c.snapshots[tickCount_ % c.snapshots.size()];
    snap.tick = tickCount_;
    game_->buildEntityStates(slot, snap.entities);

    // Delta from the newest snapshot the client confirmed, if we still have it.
    const Snapshot* base = nullptr;
    if (c.ackedTick > 0 && tickCount_ - c.ackedTick < c.snapshots.size()) {
        const Snapshot& b = c.snapshots[c.ackedTick % c.snapshots.size()];
        if (b.tick == c.ackedTick) base = &b;
    }
    static const std::vector<proto::EntityState> empty;
    net::ByteWriter w;
    w.write(proto::Svc::Snapshot);
    w.write(tickCount_);
    w.write(base ? base->tick : 0u);
    w.write(c.lastCommand);
    game_->playerState(slot).write(w);
    proto::writeEntityDelta(w, base ? base->entities : empty, snap.entities);
    c.channel->send(w.data());
}

void Server::printStatus() const {
    log::info("map: {}   tick: {}   players: {}/{}   port: {}", mapName_, tickCount_, clientCount(),
              sv_maxplayers.getInt(), listenPort());
    log::info("{:<4} {:<20} {:>6} {:>6}  {}", "slot", "name", "ping", "loss", "address");
    for (int i = 0; i < proto::kMaxClients; ++i) {
        const Client& c = clients_[static_cast<size_t>(i)];
        if (!c.active) continue;
        auto st = c.channel ? c.channel->stats() : net::NetStats{};
        log::info("{:<4} {:<20} {:>6.0f} {:>5.0f}%  {}", i, c.name, st.rttMs, st.lossPercent,
                  c.local ? "loopback" : c.address.toString());
    }
}

} // namespace k
