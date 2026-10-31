// Brush trigger volumes.

#include "entity.h"
#include "game_world.h"

#include <cstdlib>

namespace k::game {
namespace {

int toInt(const std::string& v) { return static_cast<int>(std::strtol(v.c_str(), nullptr, 10)); }

// Fires OnStartTouch / OnEndTouch / OnTrigger when the player enters. "wait" seconds between
// OnTrigger fires (-1 = only once).
class TriggerMultiple : public BaseEntity {
public:
    Output onStartTouch, onEndTouch, onTrigger;
    TriggerMultiple() {
        declareOutput("OnStartTouch", &onStartTouch);
        declareOutput("OnEndTouch", &onEndTouch);
        declareOutput("OnTrigger", &onTrigger);
    }
    bool keyValue(const std::string& k, const std::string& v) override {
        if (nameEquals(k, "wait")) { wait_ = std::strtof(v.c_str(), nullptr); return true; }
        if (nameEquals(k, "StartDisabled")) { enabled_ = toInt(v) == 0; return true; }
        return BaseEntity::keyValue(k, v);
    }
    void spawn() override {
        solid = Solid::Trigger;
        effects |= EF_NODRAW;
    }
    void startTouch(BaseEntity* other) override {
        if (!enabled_) return;
        onStartTouch.fire(other, this);
        if (world().time() >= nextTrigger_) {
            onTrigger.fire(other, this);
            if (wait_ < 0) {
                enabled_ = false;
                if (removeAfterFire()) markedForDeletion = true;
            } else {
                nextTrigger_ = world().time() + wait_;
            }
        }
    }
    void endTouch(BaseEntity* other) override {
        if (enabled_) onEndTouch.fire(other, this);
    }
    bool acceptInput(const std::string& in, BaseEntity* activator, BaseEntity* caller, const std::string& p) override {
        if (nameEquals(in, "Enable")) { enabled_ = true; return true; }
        if (nameEquals(in, "Disable")) { enabled_ = false; return true; }
        if (nameEquals(in, "Toggle")) { enabled_ = !enabled_; return true; }
        return BaseEntity::acceptInput(in, activator, caller, p);
    }

protected:
    virtual bool removeAfterFire() const { return false; }
    float wait_ = 0.2f;
    bool enabled_ = true;
    double nextTrigger_ = 0;
};
LINK_ENTITY_TO_CLASS(trigger_multiple, TriggerMultiple)

class TriggerOnce : public TriggerMultiple {
public:
    TriggerOnce() { wait_ = -1; }
    bool keyValue(const std::string& k, const std::string& v) override {
        if (nameEquals(k, "wait")) return true; // always once
        return TriggerMultiple::keyValue(k, v);
    }

protected:
    bool removeAfterFire() const override { return true; }
};
LINK_ENTITY_TO_CLASS(trigger_once, TriggerOnce)

// Moves the player to the entity named by "target" (an info_teleport_destination).
class TriggerTeleport : public TriggerMultiple {
public:
    TriggerTeleport() { wait_ = 0; }
    bool keyValue(const std::string& k, const std::string& v) override {
        if (nameEquals(k, "target")) { target_ = v; return true; }
        return TriggerMultiple::keyValue(k, v);
    }
    void startTouch(BaseEntity* other) override {
        TriggerMultiple::startTouch(other);
        if (!enabled_) return;
        auto* dest = world().findFirstByName(target_);
        auto* p = dynamic_cast<Player*>(other);
        if (!dest || !p) return;
        p->state.origin = dest->origin;
        p->state.velocity = Vec3(0);
        p->state.viewAngles = dest->angles;
        p->origin = dest->origin;
        p->teleported = true;
    }

private:
    std::string target_;
};
LINK_ENTITY_TO_CLASS(trigger_teleport, TriggerTeleport)

} // namespace
// Referenced from game_world.cpp so the linker keeps this file (and its registrations).
void linkEntities_triggers() {}

} // namespace k::game
