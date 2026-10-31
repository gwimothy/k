// Linear movers: func_door (doors, elevators, platforms) and func_button.

#include "core/keyvalues.h"
#include "core/log.h"
#include "entity.h"
#include "game_world.h"

#include <cmath>
#include <cstdlib>

namespace k::game {
namespace {

int toInt(const std::string& v) { return static_cast<int>(std::strtol(v.c_str(), nullptr, 10)); }
float toFloat(const std::string& v) { return std::strtof(v.c_str(), nullptr); }

// Moves between a closed position (where it was built) and an open position along a
// direction, at a constant speed.
class LinearMover : public BaseEntity {
public:
    enum class State { Closed, Opening, Open, Closing };

    bool keyValue(const std::string& k, const std::string& v) override {
        if (nameEquals(k, "speed")) { speed_ = std::max(1.0f, toFloat(v)); return true; }
        if (nameEquals(k, "wait")) { wait_ = toFloat(v); return true; }
        if (nameEquals(k, "lip")) { lip_ = toFloat(v); return true; }
        if (nameEquals(k, "movedir")) {
            Vec3 a = parseVec3(v);
            moveAngles_ = {a.x, a.y, a.z};
            hasMoveDir_ = true;
            return true;
        }
        if (nameEquals(k, "locked")) { locked_ = toInt(v) != 0; return true; }
        if (nameEquals(k, "spawnpos")) { startOpen_ = toInt(v) != 0; return true; }
        if (nameEquals(k, "spawnflags")) { spawnflags_ = toInt(v); return true; }
        return BaseEntity::keyValue(k, v);
    }

    void spawn() override {
        solid = Solid::Bsp;
        networked = true;
        // Direction: "movedir", else the entity's angles (Source: "-90 0 0" moves up).
        QAngle dirAngles = hasMoveDir_ ? moveAngles_ : angles;
        angleVectors(dirAngles, &moveDir_);
        if (dirAngles.pitch == -90) moveDir_ = {0, 0, 1};
        if (dirAngles.pitch == 90) moveDir_ = {0, 0, -1};
        angles = {};
        // Distance: the brush size along the direction, minus the lip.
        Vec3 size(0);
        if (world().map() && brushModel > 0 && static_cast<size_t>(brushModel) < world().map()->models.size()) {
            const auto& m = world().map()->models[static_cast<size_t>(brushModel)];
            size = m.maxs - m.mins;
        }
        float distance = std::fabs(moveDir_.x * size.x) + std::fabs(moveDir_.y * size.y) + std::fabs(moveDir_.z * size.z) - lip_;
        pos1_ = origin;
        pos2_ = origin + moveDir_ * std::max(0.0f, distance);
        if (startOpen_) {
            origin = pos2_;
            std::swap(pos1_, pos2_);
        }
    }

    void think() override {
        if (state_ != State::Opening && state_ != State::Closing) {
            // Waiting at the open position: time to close.
            if (state_ == State::Open && wait_ >= 0) startMove(false);
            return;
        }
        const Vec3& dest = state_ == State::Opening ? pos2_ : pos1_;
        Vec3 delta = dest - origin;
        float dist = glm::length(delta);
        float step = speed_ * kTick;
        bool arrive = dist <= step;
        Vec3 move = arrive ? delta : delta / dist * step;
        if (!world().tryPush(*this, move)) {
            // Blocked by the player: wait and try again (Source can also reverse; see "blocked").
            setNextThink(world().time() + kTick);
            return;
        }
        if (arrive) {
            origin = dest;
            if (state_ == State::Opening) {
                state_ = State::Open;
                reachedOpen();
                if (wait_ >= 0) setNextThink(world().time() + wait_);
            } else {
                state_ = State::Closed;
                reachedClosed();
            }
            return;
        }
        setNextThink(world().time() + kTick);
    }

    void startMove(bool open) {
        if (open && (state_ == State::Open || state_ == State::Opening)) {
            // Reset the auto-close timer.
            if (state_ == State::Open && wait_ >= 0) setNextThink(world().time() + wait_);
            return;
        }
        if (!open && (state_ == State::Closed || state_ == State::Closing)) return;
        state_ = open ? State::Opening : State::Closing;
        if (open) startedOpening();
        else startedClosing();
        setNextThink(world().time() + kTick);
    }

    State moverState() const { return state_; }

protected:
    static constexpr double kTick = 1.0 / 66.0;
    virtual void startedOpening() {}
    virtual void startedClosing() {}
    virtual void reachedOpen() {}
    virtual void reachedClosed() {}

    float speed_ = 100, wait_ = 4, lip_ = 0;
    bool locked_ = false, startOpen_ = false, hasMoveDir_ = false;
    int spawnflags_ = 0;
    QAngle moveAngles_;
    Vec3 moveDir_{0, 0, 1}, pos1_{0}, pos2_{0};
    State state_ = State::Closed;
};

// Doors, elevators and platforms. Opened by inputs (or +use with spawnflag 256).
class FuncDoor : public LinearMover {
public:
    Output onOpen, onClose, onFullyOpen, onFullyClosed, onLockedUse, onBlocked;
    FuncDoor() {
        declareOutput("OnOpen", &onOpen);
        declareOutput("OnClose", &onClose);
        declareOutput("OnFullyOpen", &onFullyOpen);
        declareOutput("OnFullyClosed", &onFullyClosed);
        declareOutput("OnLockedUse", &onLockedUse);
        declareOutput("OnBlocked", &onBlocked);
    }
    bool acceptInput(const std::string& in, BaseEntity* activator, BaseEntity* caller, const std::string& p) override {
        if (nameEquals(in, "Open")) { if (!locked_) startMove(true); return true; }
        if (nameEquals(in, "Close")) { startMove(false); return true; }
        if (nameEquals(in, "Toggle")) {
            if (!locked_) startMove(state_ == State::Closed || state_ == State::Closing);
            return true;
        }
        if (nameEquals(in, "Lock")) { locked_ = true; return true; }
        if (nameEquals(in, "Unlock")) { locked_ = false; return true; }
        if (nameEquals(in, "SetSpeed")) { speed_ = std::max(1.0f, toFloat(p)); return true; }
        return LinearMover::acceptInput(in, activator, caller, p);
    }
    void use(BaseEntity* activator) override {
        if (!(spawnflags_ & 256)) return; // "Use Opens"
        if (locked_) { onLockedUse.fire(activator, this); return; }
        startMove(state_ == State::Closed || state_ == State::Closing);
    }
    void blocked(BaseEntity* other) override { onBlocked.fire(other, this); }

protected:
    void startedOpening() override { onOpen.fire(this, this); }
    void startedClosing() override { onClose.fire(this, this); }
    void reachedOpen() override { onFullyOpen.fire(this, this); }
    void reachedClosed() override { onFullyClosed.fire(this, this); }
};
LINK_ENTITY_TO_CLASS(func_door, FuncDoor)

// Pressed with +use: moves in a little, fires OnPressed, comes back out after "wait".
class FuncButton : public LinearMover {
public:
    Output onPressed, onIn, onOut, onUseLocked;
    FuncButton() {
        declareOutput("OnPressed", &onPressed);
        declareOutput("OnIn", &onIn);
        declareOutput("OnOut", &onOut);
        declareOutput("OnUseLocked", &onUseLocked);
        speed_ = 40;
        wait_ = 1;
    }
    void spawn() override {
        // Buttons travel 4 units unless a lip says otherwise.
        bool customLip = lip_ != 0;
        LinearMover::spawn();
        if (!customLip) pos2_ = pos1_ + moveDir_ * 4.0f;
    }
    void use(BaseEntity* activator) override {
        if (locked_) { onUseLocked.fire(activator, this); return; }
        if (state_ != State::Closed) return;
        activator_ = activator ? activator->index : -1;
        onPressed.fire(activator, this);
        startMove(true);
    }
    bool acceptInput(const std::string& in, BaseEntity* activator, BaseEntity* caller, const std::string& p) override {
        if (nameEquals(in, "Press")) { use(activator); return true; }
        if (nameEquals(in, "Lock")) { locked_ = true; return true; }
        if (nameEquals(in, "Unlock")) { locked_ = false; return true; }
        return LinearMover::acceptInput(in, activator, caller, p);
    }

protected:
    void reachedOpen() override { onIn.fire(world().entity(activator_), this); }
    void reachedClosed() override { onOut.fire(world().entity(activator_), this); }
    int activator_ = -1;
};
LINK_ENTITY_TO_CLASS(func_button, FuncButton)

} // namespace
// Referenced from game_world.cpp so the linker keeps this file (and its registrations).
void linkEntities_movers() {}

} // namespace k::game
