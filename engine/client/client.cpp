#include "client/client.h"

#include "client/kbutton.h"
#include "core/cvar.h"
#include "core/log.h"

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

} // namespace

void Client::connect(std::unique_ptr<net::NetChannel> channel, std::string playerName) {
    disconnect();
    channel_ = std::move(channel);
    net::ByteWriter w;
    w.write(proto::Clc::Connect);
    w.write(proto::kProtocolVersion);
    w.writeString(playerName);
    channel_->send(w.data());
}

void Client::disconnect() {
    channel_.reset();
    gotServerInfo_ = false;
    snapshots_ = 0;
    commandNumber_ = 0;
    previous_ = current_ = {};
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

void Client::createMove(float /*tickInterval*/) {
    if (!gotServerInfo_) return;
    auto& b = buttons();
    UserCmd cmd;
    cmd.commandNumber = ++commandNumber_;
    cmd.tickCount = serverTick_;
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

    net::ByteWriter w;
    w.write(proto::Clc::Move);
    w.write<uint8_t>(1);
    cmd.write(w);
    channel_->send(w.data());
}

void Client::readPackets() {
    if (!channel_) return;
    while (auto msg = channel_->receive()) {
        net::ByteReader r(*msg);
        while (!r.atEnd() && !r.overflowed()) {
            auto type = static_cast<proto::Svc>(r.read<uint8_t>());
            switch (type) {
                case proto::Svc::ServerInfo: {
                    auto version = r.read<uint32_t>();
                    serverTickInterval_ = r.read<float>();
                    mapName_ = r.readString();
                    if (version != proto::kProtocolVersion) {
                        log::error("Server protocol {} doesn't match ours ({})", version, proto::kProtocolVersion);
                        disconnect();
                        return;
                    }
                    gotServerInfo_ = true;
                    log::info("Connected to server: map '{}', {:.0f} tick", mapName_, 1.0f / serverTickInterval_);
                    break;
                }
                case proto::Svc::Snapshot: {
                    serverTick_ = r.read<uint32_t>();
                    r.read<uint32_t>(); // last acked command, used for prediction later
                    auto state = proto::PlayerState::read(r);
                    previous_ = snapshots_ == 0 ? state : current_;
                    current_ = state;
                    ++snapshots_;
                    break;
                }
                case proto::Svc::Print: log::info("{}", r.readString()); break;
                default:
                    log::warn("Client: bad server message {}", static_cast<int>(type));
                    return;
            }
        }
    }
}

ViewSetup Client::view(float alpha) const {
    ViewSetup v;
    float t = cl_interpolate.getBool() ? std::clamp(alpha, 0.0f, 1.0f) : 1.0f;
    Vec3 origin = glm::mix(previous_.origin, current_.origin, t);
    float viewHeight = glm::mix(previous_.viewHeight, current_.viewHeight, t);
    v.origin = origin + Vec3(0, 0, viewHeight);
    // Use the freshest local angles so mouse look has no added latency.
    v.angles = viewAngles_;
    v.fov = fov_desired.getFloat();
    return v;
}

} // namespace k
