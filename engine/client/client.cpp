#include "client/client.h"

#include "client/kbutton.h"
#include "core/cvar.h"
#include "core/log.h"
#include "core/time.h"
#include "shared/bsp_trace.h"
#include "shared/gamemovement.h"

#include <algorithm>

namespace k {
namespace {

ConVar sensitivity("sensitivity", "3", FCVAR_ARCHIVE, "Mouse sensitivity.", 0.0001f, 100.0f);
ConVar m_pitch("m_pitch", "0.022", FCVAR_ARCHIVE, "Mouse pitch factor (negative inverts).");
ConVar m_yaw("m_yaw", "0.022", FCVAR_ARCHIVE, "Mouse yaw factor.");
ConVar cl_pitchup("cl_pitchup", "89", FCVAR_CHEAT, "Max look-up angle.");
ConVar cl_pitchdown("cl_pitchdown", "89", FCVAR_CHEAT, "Max look-down angle.");
ConVar cl_forwardspeed("cl_forwardspeed", "450", FCVAR_ARCHIVE | FCVAR_CHEAT, "Forward/back move speed.");
ConVar cl_sidespeed("cl_sidespeed", "450", FCVAR_ARCHIVE | FCVAR_CHEAT, "Strafe speed.");
ConVar cl_upspeed("cl_upspeed", "320", FCVAR_ARCHIVE | FCVAR_CHEAT, "Up/down speed (noclip, swimming).");
ConVar fov_desired("fov_desired", "90", FCVAR_ARCHIVE, "Field of view (horizontal degrees at 4:3).", 75.0f, 110.0f);
ConVar cl_yawspeed("cl_yawspeed", "210", FCVAR_NONE, "Keyboard turn speed (+left/+right), degrees/sec.");
ConVar cl_pitchspeed("cl_pitchspeed", "225", FCVAR_NONE, "Keyboard look speed (+lookup/+lookdown), degrees/sec.");
ConVar cl_interpolate("cl_interpolate", "1", FCVAR_NONE, "Smooth movement between server ticks.");
ConVar cl_interp("cl_interp", "0.1", FCVAR_ARCHIVE, "How far in the past other entities are drawn (seconds).", 0.0f, 0.5f);
ConVar cl_predict("cl_predict", "1", FCVAR_NONE, "Predict your own movement.");
ConVar cl_smoothtime("cl_smoothtime", "0.1", FCVAR_NONE, "Seconds to smooth out prediction errors.", 0.0f, 1.0f);
ConVar cl_showerror("cl_showerror", "0", FCVAR_NONE, "Log prediction errors.");

// ---- Buttons ----------------------------------------------------------------------

struct Buttons {
    KButton forward, back, moveleft, moveright, moveup, movedown;
    KButton jump, duck, speed, walk, attack, attack2, use, reload;
    KButton left, right, lookup, lookdown;
};

Buttons& buttons() {
    static Buttons b;
    return b;
}

// Registers "+name"/"-name" commands for a button.
struct ButtonCommands {
    ConCommand down, up;
    ButtonCommands(const std::string& name, KButton Buttons::*member)
        : down("+" + name, [member](const CmdArgs& a) { (buttons().*member).press(a.arg(1)); }, "Hold " + name + "."),
          up("-" + name, [member](const CmdArgs& a) { (buttons().*member).release(a.arg(1)); }, "Release " + name + ".") {}
};

ButtonCommands bc_forward("forward", &Buttons::forward);
ButtonCommands bc_back("back", &Buttons::back);
ButtonCommands bc_moveleft("moveleft", &Buttons::moveleft);
ButtonCommands bc_moveright("moveright", &Buttons::moveright);
ButtonCommands bc_moveup("moveup", &Buttons::moveup);
ButtonCommands bc_movedown("movedown", &Buttons::movedown);
ButtonCommands bc_jump("jump", &Buttons::jump);
ButtonCommands bc_duck("duck", &Buttons::duck);
ButtonCommands bc_speed("speed", &Buttons::speed);
ButtonCommands bc_walk("walk", &Buttons::walk);
ButtonCommands bc_attack("attack", &Buttons::attack);
ButtonCommands bc_attack2("attack2", &Buttons::attack2);
ButtonCommands bc_use("use", &Buttons::use);
ButtonCommands bc_reload("reload", &Buttons::reload);
ButtonCommands bc_left("left", &Buttons::left);
ButtonCommands bc_right("right", &Buttons::right);
ButtonCommands bc_lookup("lookup", &Buttons::lookup);
ButtonCommands bc_lookdown("lookdown", &Buttons::lookdown);


float cvarFloat(const char* name, float def) {
    auto* v = console::findVar(name);
    return v ? v->getFloat() : def;
}

MoveParams moveParamsFromCvars() {
    MoveParams p;
    p.maxSpeed = cvarFloat("sv_maxspeed", p.maxSpeed);
    p.accelerate = cvarFloat("sv_accelerate", p.accelerate);
    p.airAccelerate = cvarFloat("sv_airaccelerate", p.airAccelerate);
    p.friction = cvarFloat("sv_friction", p.friction);
    p.stopSpeed = cvarFloat("sv_stopspeed", p.stopSpeed);
    p.gravity = cvarFloat("sv_gravity", p.gravity);
    p.stepSize = cvarFloat("sv_stepsize", p.stepSize);
    p.jumpImpulse = std::sqrt(2.0f * p.gravity * 57.0f);
    p.autoBunnyhop = cvarFloat("sv_autobunnyhopping", 0) != 0;
    p.noclipSpeed = cvarFloat("sv_noclipspeed", p.noclipSpeed);
    p.noclipAccelerate = cvarFloat("sv_noclipaccelerate", p.noclipAccelerate);
    return p;
}

// What the client's prediction collides with: the map and the brush entities as the
// latest snapshot placed them. (Props and other players are left to the server.)
class ClientTraceWorld final : public TraceWorld {
public:
    ClientTraceWorld(const bsp::BspData& map, const std::vector<std::pair<int, Vec3>>& brushes)
        : map_(map), brushes_(brushes) {}
    bsp::TraceResult traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                              uint32_t mask) const override {
        auto best = bsp::traceBox(map_, start, end, mins, maxs, mask);
        for (auto& [model, origin] : brushes_) {
            auto tr = bsp::traceBoxModel(map_, model, origin, start, end, mins, maxs, mask);
            if (tr.allSolid || tr.fraction < best.fraction) {
                best = tr;
                best.entity = model;
            }
            best.startSolid |= tr.startSolid;
        }
        return best;
    }

private:
    const bsp::BspData& map_;
    const std::vector<std::pair<int, Vec3>>& brushes_;
};

} // namespace

void Client::connectLocal(std::unique_ptr<net::NetChannel> channel, std::string playerName) {
    disconnect();
    remote_ = false;
    channel_ = std::move(channel);
    playerName_ = std::move(playerName);
    state_ = State::Connecting;
    net::ByteWriter w;
    w.write(proto::Clc::Connect);
    w.write(proto::kProtocolVersion);
    w.writeString(playerName_);
    channel_->sendReliable(w.data());
}

bool Client::connectRemote(const net::Address& server, std::string playerName, const std::string& password) {
    disconnect();
    if (!socket_.open(0)) return false;
    remote_ = true;
    serverAddress_ = server;
    playerName_ = std::move(playerName);
    password_ = password;
    state_ = State::Connecting;
    nextConnectAttempt_ = 0;
    connectDeadline_ = platformTime() + 10.0;
    log::info("Connecting to {}...", server.toString());
    return true;
}

void Client::sendConnectPacket(double now) {
    std::string name = playerName_;
    std::replace(name.begin(), name.end(), '"', '\'');
    net::sendConnectionless(socket_, serverAddress_,
                            "connect " + std::to_string(proto::kProtocolVersion) + " \"" + name + "\"" +
                                (password_.empty() ? "" : " \"" + password_ + "\""));
    nextConnectAttempt_ = now + 0.5;
}

void Client::disconnect(const std::string& reason) {
    if (channel_ && connected()) {
        net::ByteWriter w;
        w.write(proto::Clc::Disconnect);
        channel_->sendReliable(w.data());
        channel_->transmit(platformTime());
        channel_->transmit(platformTime());
    }
    if (!reason.empty()) {
        disconnectReason_ = reason;
        log::info("Disconnected: {}", reason);
    }
    channel_.reset();
    udp_ = nullptr;
    socket_.close();
    state_ = State::Disconnected;
    remote_ = false;
    mapPending_ = false;
    map_.reset();
    commandNumber_ = 0;
    commands_.clear();
    for (auto& s : snapshots_) s = {};
    latestTick_ = 0;
    haveClock_ = false;
    predicted_ = previousPredicted_ = {};
    predictionError_ = Vec3(0);
    predictedHistory_.clear();
    modelTable_.clear();
    centerMessage_.clear();
    slot_ = -1;
}

std::optional<std::string> Client::takeMapToLoad() {
    if (!mapPending_) return std::nullopt;
    mapPending_ = false;
    return mapName_;
}

void Client::frameKeyboardLook(float dt) {
    auto& b = buttons();
    viewAngles_.yaw = normalizeAngle(viewAngles_.yaw + cl_yawspeed.getFloat() * dt * (b.left.state() - b.right.state()));
    viewAngles_.pitch = std::clamp(viewAngles_.pitch + cl_pitchspeed.getFloat() * dt * (b.lookdown.state() - b.lookup.state()),
                                   -cl_pitchup.getFloat(), cl_pitchdown.getFloat());
}

void Client::frameMouse(float dx, float dy) {
    float s = sensitivity.getFloat();
    viewAngles_.yaw = normalizeAngle(viewAngles_.yaw - dx * s * m_yaw.getFloat());
    viewAngles_.pitch = std::clamp(viewAngles_.pitch + dy * s * m_pitch.getFloat(), -cl_pitchup.getFloat(),
                                   cl_pitchdown.getFloat());
}

void Client::tick(double now, float frameTime) {
    if (state_ == State::Connecting && remote_ && !udp_) {
        if (now > connectDeadline_) {
            disconnect("Couldn't connect to " + serverAddress_.toString());
            return;
        }
        if (now >= nextConnectAttempt_) sendConnectPacket(now);
        return;
    }
    if (!channel_) return;
    if (channel_->timedOut(now)) {
        disconnect("Connection timed out");
        return;
    }
    (void)frameTime;
    if (state_ == State::Active && map_) {
        auto& b = buttons();
        UserCmd cmd;
        cmd.commandNumber = ++commandNumber_;
        // The server tick we're showing other entities at: used for lag compensation.
        double renderTime = clientTime_ - cl_interp.getFloat();
        cmd.tickCount = static_cast<uint32_t>(std::max(0.0, renderTime / serverTickInterval_));
        cmd.viewAngles = viewAngles_;
        cmd.forwardMove = cl_forwardspeed.getFloat() * (b.forward.state() - b.back.state());
        cmd.sideMove = cl_sidespeed.getFloat() * (b.moveright.state() - b.moveleft.state());
        cmd.upMove = cl_upspeed.getFloat() * (b.moveup.state() + b.jump.state() - b.movedown.state() - b.duck.state());
        auto bit = [&](const KButton& k, uint32_t flag) {
            if (k.down || k.pressedThisTick) cmd.buttons |= flag;
        };
        bit(b.forward, IN_FORWARD);
        bit(b.back, IN_BACK);
        bit(b.moveleft, IN_MOVELEFT);
        bit(b.moveright, IN_MOVERIGHT);
        bit(b.jump, IN_JUMP);
        bit(b.duck, IN_DUCK);
        bit(b.speed, IN_SPEED);
        bit(b.walk, IN_WALK);
        bit(b.attack, IN_ATTACK);
        bit(b.attack2, IN_ATTACK2);
        bit(b.use, IN_USE);
        bit(b.reload, IN_RELOAD);
        for (KButton* k : {&b.forward, &b.back, &b.moveleft, &b.moveright, &b.moveup, &b.movedown, &b.jump, &b.duck,
                           &b.speed, &b.walk, &b.attack, &b.attack2, &b.use, &b.reload})
            k->pressedThisTick = false;
        commands_.push_back(cmd);
        while (commands_.size() > 128) commands_.pop_front();

        // Send the newest few commands so a lost packet doesn't lose input.
        net::ByteWriter w;
        w.write(proto::Clc::Move);
        w.write(latestTick_);
        uint8_t count = static_cast<uint8_t>(std::min<size_t>(commands_.size(), 3));
        w.write(count);
        for (size_t i = commands_.size() - count; i < commands_.size(); ++i) commands_[i].write(w);
        channel_->send(w.data());
        predict();
    }
    channel_->transmit(now);
}

void Client::advance(float frameTime) {
    if (haveClock_) clientTime_ += frameTime;
    float smooth = cl_smoothtime.getFloat();
    float keep = smooth > 0 ? std::max(0.0f, 1.0f - frameTime / smooth) : 0.0f;
    predictionError_ *= keep;
}

void Client::sendStringCmd(const std::string& text) {
    if (!channel_) return;
    net::ByteWriter w;
    w.write(proto::Clc::StringCmd);
    w.writeString(text);
    channel_->sendReliable(w.data());
}

void Client::readPackets(double now) {
    if (remote_ && socket_.isOpen()) {
        net::Address from;
        while (auto packet = socket_.receiveFrom(from)) {
            if (!(from == serverAddress_)) continue;
            if (auto text = net::readConnectionless(*packet)) {
                if (text->rfind("accept", 0) == 0 && !udp_) {
                    auto chan = std::make_unique<net::UdpChannel>(
                        [this](std::span<const uint8_t> d) { socket_.sendTo(serverAddress_, d); }, now);
                    udp_ = chan.get();
                    channel_ = std::move(chan);
                    log::info("Connection accepted by {}", serverAddress_.toString());
                } else if (text->rfind("reject", 0) == 0) {
                    disconnect("Server rejected the connection: " + text->substr(std::min<size_t>(7, text->size())));
                    return;
                }
                continue;
            }
            if (udp_) udp_->processPacket(*packet, now);
        }
    }
    if (!channel_) return;
    while (auto msg = channel_->receive()) {
        net::ByteReader r(*msg);
        while (!r.atEnd() && !r.overflowed() && channel_) handleMessage(r, now);
        if (!channel_) return; // disconnected while handling
    }
}

void Client::handleMessage(net::ByteReader& r, double now) {
    auto type = static_cast<proto::Svc>(r.read<uint8_t>());
    switch (type) {
        case proto::Svc::ServerInfo: {
            auto version = r.read<uint32_t>();
            serverTickInterval_ = r.read<float>();
            mapName_ = r.readString();
            modelTable_.clear();
            for (uint16_t n = r.read<uint16_t>(), i = 0; i < n && !r.overflowed(); ++i) modelTable_.push_back(r.readString());
            for (uint16_t n = r.read<uint16_t>(), i = 0; i < n && !r.overflowed(); ++i) {
                std::string name = r.readString(), value = r.readString();
                // A listen server shares these cvars with us already.
                if (auto* v = console::findVar(name); v && remote_) v->forceValue(value);
            }
            slot_ = r.read<uint8_t>();
            if (version != proto::kProtocolVersion) {
                disconnect("Server protocol doesn't match");
                return;
            }
            state_ = State::Connected;
            mapPending_ = true;
            log::info("Connected to server: map '{}', {:.0f} tick", mapName_, 1.0f / serverTickInterval_);
            break;
        }
        case proto::Svc::Snapshot: readSnapshot(r, now); break;
        case proto::Svc::Print: log::info("{}", r.readString()); break;
        case proto::Svc::SetAngle: viewAngles_ = r.read<QAngle>(); break;
        case proto::Svc::CenterPrint:
            centerMessage_ = r.readString();
            centerMessageTime_ = platformTime();
            log::info("{}", centerMessage_);
            break;
        case proto::Svc::Sound: {
            SoundEvent ev;
            ev.name = r.readString();
            ev.entity = r.read<uint16_t>();
            ev.origin = r.read<Vec3>();
            ev.volume = r.read<float>();
            ev.stop = (r.read<uint8_t>() & 1) != 0;
            sounds_.push_back(std::move(ev));
            break;
        }
        case proto::Svc::SetCvar: {
            std::string name = r.readString(), value = r.readString();
            if (auto* v = console::findVar(name); v && remote_) v->forceValue(value);
            break;
        }
        case proto::Svc::Disconnect: disconnect("Server: " + r.readString()); return;
        default:
            log::warn("Client: bad server message {}", static_cast<int>(type));
            r.read<uint64_t>(); // force overflow to stop parsing this message
            break;
    }
}

const Client::Snapshot* Client::findSnapshot(uint32_t tick) const {
    const Snapshot& s = snapshots_[tick % snapshots_.size()];
    return (s.valid && s.tick == tick) ? &s : nullptr;
}

void Client::readSnapshot(net::ByteReader& r, double now) {
    Snapshot snap;
    snap.tick = r.read<uint32_t>();
    uint32_t deltaFrom = r.read<uint32_t>();
    snap.lastCommand = r.read<uint32_t>();
    snap.player = proto::PlayerState::read(r);
    static const std::vector<proto::EntityState> empty;
    const Snapshot* base = deltaFrom ? findSnapshot(deltaFrom) : nullptr;
    if (deltaFrom && !base) {
        // We no longer have the baseline: skip it; the server falls back to our last ack.
        r.read<uint64_t>(); r.read<uint64_t>(); // poison the reader so the rest is ignored
        return;
    }
    if (!proto::readEntityDelta(r, base ? base->entities : empty, snap.entities)) return;
    if (snap.tick <= latestTick_ && latestTick_ != 0) return; // old
    snap.valid = true;
    snapshots_[snap.tick % snapshots_.size()] = snap;
    latestTick_ = snap.tick;

    // Clock: keep our estimate of server time close to the newest snapshot.
    double serverTime = snap.tick * static_cast<double>(serverTickInterval_);
    if (!haveClock_ || std::fabs(clientTime_ - serverTime) > 0.25) {
        clientTime_ = serverTime;
        haveClock_ = true;
    } else {
        clientTime_ += (serverTime - clientTime_) * 0.05;
    }

    // Acknowledged commands are history now.
    while (!commands_.empty() && commands_.front().commandNumber <= snap.lastCommand) commands_.pop_front();

    if (state_ == State::Connected && map_) state_ = State::Active;
    if (state_ != State::Active) {
        predicted_ = previousPredicted_ = snap.player;
        return;
    }

    // Compare the server's result for its last command with what we predicted for it.
    Vec3 oldPrediction = predicted_.origin;
    bool havePrevious = false;
    for (auto& [num, origin] : predictedHistory_)
        if (num == snap.lastCommand) {
            oldPrediction = origin;
            havePrevious = true;
        }
    std::erase_if(predictedHistory_, [&](const auto& p) { return p.first <= snap.lastCommand; });
    if (havePrevious) {
        Vec3 err = oldPrediction - snap.player.origin;
        float size = glm::length(err);
        if (size > 64.0f) { // teleport: snap, don't smooth or report
            err = Vec3(0);
            size = 0;
        }
        if (size > 0.01f) {
            predictionError_ += err;
            lastErrorSize_ = size;
            if (cl_showerror.getBool()) log::info("prediction error {:.2f} units (cmd {})", size, snap.lastCommand);
        }
    }
    predict();

    ++snapshotsThisSecond_;
    if (now - snapshotCounterStart_ >= 1.0) {
        snapshotsPerSec_ = snapshotsThisSecond_;
        snapshotsThisSecond_ = 0;
        snapshotCounterStart_ = now;
    }
}

void Client::predict() {
    const Snapshot* latest = findSnapshot(latestTick_);
    if (!latest) return;
    previousPredicted_ = predicted_;
    proto::PlayerState s = latest->player;
    if (cl_predict.getBool() && map_) {
        std::vector<std::pair<int, Vec3>> brushes;
        for (auto& e : latest->entities) {
            if (e.model == 0 || e.model >= modelTable_.size() || (e.effects & 1u)) continue;
            const std::string& m = modelTable_[e.model];
            if (!m.empty() && m[0] == '*') brushes.emplace_back(std::atoi(m.c_str() + 1), e.origin);
        }
        ClientTraceWorld world(*map_, brushes);
        MoveParams params = moveParamsFromCvars();
        predictedHistory_.clear();
        for (auto& cmd : commands_) {
            playerMove(s, cmd, serverTickInterval_, params, &world);
            predictedHistory_.emplace_back(cmd.commandNumber, s.origin);
        }
    }
    predicted_ = s;
}

ViewSetup Client::view(float alpha) const {
    ViewSetup v;
    float t = cl_interpolate.getBool() ? std::clamp(alpha, 0.0f, 1.0f) : 1.0f;
    Vec3 origin = glm::mix(previousPredicted_.origin, predicted_.origin, t);
    float viewHeight = glm::mix(previousPredicted_.viewHeight, predicted_.viewHeight, t);
    v.origin = origin + Vec3(0, 0, viewHeight) + predictionError_;
    // Use the freshest local angles so mouse look has no added latency.
    v.angles = viewAngles_;
    v.fov = fov_desired.getFloat();
    return v;
}

void Client::renderEntities(std::vector<RenderEntity>& out) const {
    out.clear();
    if (!latestTick_) return;
    // Find the two snapshots around the render time and blend.
    double renderTime = clientTime_ - (cl_interpolate.getBool() ? cl_interp.getFloat() : 0.0f);
    double renderTick = renderTime / serverTickInterval_;
    const Snapshot *from = nullptr, *to = nullptr;
    for (const auto& s : snapshots_) {
        if (!s.valid) continue;
        if (s.tick <= renderTick && (!from || s.tick > from->tick)) from = &s;
        if (s.tick > renderTick && (!to || s.tick < to->tick)) to = &s;
    }
    if (!from) from = to;
    if (!to) to = from;
    if (!from) return;
    float t = (to->tick == from->tick) ? 1.0f
                                       : static_cast<float>((renderTick - from->tick) / static_cast<double>(to->tick - from->tick));
    t = std::clamp(t, 0.0f, 1.0f);
    for (auto& cur : to->entities) {
        if ((cur.effects & 1u) || cur.model == 0 || cur.model >= modelTable_.size()) continue;
        RenderEntity e;
        e.index = cur.index;
        e.model = modelTable_[cur.model];
        e.origin = cur.origin;
        e.angles = cur.angles;
        auto it = std::lower_bound(from->entities.begin(), from->entities.end(), cur.index,
                                   [](const proto::EntityState& a, uint16_t i) { return a.index < i; });
        if (it != from->entities.end() && it->index == cur.index) {
            e.origin = glm::mix(it->origin, cur.origin, t);
            e.angles = lerpAngles(it->angles, cur.angles, t);
        }
        out.push_back(std::move(e));
    }
}

Client::NetGraph Client::netGraph() const {
    NetGraph g;
    if (channel_) g.net = channel_->stats();
    g.predictionError = lastErrorSize_;
    g.snapshotsPerSec = snapshotsPerSec_;
    g.interpMs = cl_interp.getFloat() * 1000.0f;
    return g;
}

} // namespace k
