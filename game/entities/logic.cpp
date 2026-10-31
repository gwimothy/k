// Logic entities: relays, timers, counters, map-spawn hooks, messages.

#include "core/log.h"
#include "entity.h"
#include "game_world.h"

#include <algorithm>
#include <cstdlib>

namespace k::game {
namespace {

int toInt(const std::string& v) { return static_cast<int>(std::strtol(v.c_str(), nullptr, 10)); }
float toFloat(const std::string& v) { return std::strtof(v.c_str(), nullptr); }

// Fires OnTrigger when it receives Trigger. The glue of most map logic.
class LogicRelay : public BaseEntity {
public:
    Output onTrigger, onSpawn;
    LogicRelay() {
        declareOutput("OnTrigger", &onTrigger);
        declareOutput("OnSpawn", &onSpawn);
    }
    bool keyValue(const std::string& k, const std::string& v) override {
        if (nameEquals(k, "StartDisabled")) { enabled_ = toInt(v) == 0; return true; }
        if (nameEquals(k, "spawnflags")) { fireOnce_ = (toInt(v) & 1) != 0; return true; }
        return BaseEntity::keyValue(k, v);
    }
    void activate() override { onSpawn.fire(this, this); }
    bool acceptInput(const std::string& in, BaseEntity* activator, BaseEntity* caller, const std::string& p) override {
        if (nameEquals(in, "Trigger")) {
            if (enabled_) {
                onTrigger.fire(activator, this);
                if (fireOnce_) enabled_ = false;
            }
            return true;
        }
        if (nameEquals(in, "Enable")) { enabled_ = true; return true; }
        if (nameEquals(in, "Disable")) { enabled_ = false; return true; }
        if (nameEquals(in, "Toggle")) { enabled_ = !enabled_; return true; }
        return BaseEntity::acceptInput(in, activator, caller, p);
    }

private:
    bool enabled_ = true;
    bool fireOnce_ = false;
};
LINK_ENTITY_TO_CLASS(logic_relay, LogicRelay)

// Fires OnTimer every RefireTime seconds while enabled.
class LogicTimer : public BaseEntity {
public:
    Output onTimer;
    LogicTimer() { declareOutput("OnTimer", &onTimer); }
    bool keyValue(const std::string& k, const std::string& v) override {
        if (nameEquals(k, "RefireTime")) { refire_ = std::max(0.01f, toFloat(v)); return true; }
        if (nameEquals(k, "StartDisabled")) { enabled_ = toInt(v) == 0; return true; }
        return BaseEntity::keyValue(k, v);
    }
    void activate() override {
        if (enabled_) setNextThink(world().time() + refire_);
    }
    void think() override {
        if (!enabled_) return;
        onTimer.fire(this, this);
        setNextThink(world().time() + refire_);
    }
    bool acceptInput(const std::string& in, BaseEntity* activator, BaseEntity* caller, const std::string& p) override {
        if (nameEquals(in, "Enable")) {
            if (!enabled_) setNextThink(world().time() + refire_);
            enabled_ = true;
            return true;
        }
        if (nameEquals(in, "Disable")) { enabled_ = false; setNextThink(0); return true; }
        if (nameEquals(in, "Toggle")) return acceptInput(enabled_ ? "Disable" : "Enable", activator, caller, p);
        if (nameEquals(in, "FireTimer")) { onTimer.fire(activator, this); return true; }
        if (nameEquals(in, "RefireTime")) { refire_ = std::max(0.01f, toFloat(p)); return true; }
        return BaseEntity::acceptInput(in, activator, caller, p);
    }

private:
    float refire_ = 1.0f;
    bool enabled_ = true;
};
LINK_ENTITY_TO_CLASS(logic_timer, LogicTimer)

// Fires OnMapSpawn once when the map starts.
class LogicAuto : public BaseEntity {
public:
    Output onMapSpawn;
    LogicAuto() { declareOutput("OnMapSpawn", &onMapSpawn); }
    void activate() override { setNextThink(world().time() + 0.01); }
    void think() override { onMapSpawn.fire(this, this); }
};
LINK_ENTITY_TO_CLASS(logic_auto, LogicAuto)

// An integer counter with limits: Add/Subtract/SetValue, OnHitMax/OnHitMin, OutValue.
class MathCounter : public BaseEntity {
public:
    Output onHitMax, onHitMin, outValue;
    MathCounter() {
        declareOutput("OnHitMax", &onHitMax);
        declareOutput("OnHitMin", &onHitMin);
        declareOutput("OutValue", &outValue);
    }
    bool keyValue(const std::string& k, const std::string& v) override {
        if (nameEquals(k, "startvalue")) { value_ = toFloat(v); return true; }
        if (nameEquals(k, "min")) { min_ = toFloat(v); return true; }
        if (nameEquals(k, "max")) { max_ = toFloat(v); return true; }
        return BaseEntity::keyValue(k, v);
    }
    bool acceptInput(const std::string& in, BaseEntity* activator, BaseEntity* caller, const std::string& p) override {
        float v = toFloat(p.empty() ? "1" : p);
        if (nameEquals(in, "Add")) set(value_ + v, activator);
        else if (nameEquals(in, "Subtract")) set(value_ - v, activator);
        else if (nameEquals(in, "SetValue")) set(toFloat(p), activator);
        else return BaseEntity::acceptInput(in, activator, caller, p);
        return true;
    }
    float value() const { return value_; }

private:
    void set(float v, BaseEntity* activator) {
        if (max_ > min_) v = std::clamp(v, min_, max_);
        bool changed = v != value_;
        value_ = v;
        if (changed) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", value_);
            outValue.fire(activator, this, buf);
        }
        if (max_ > min_ && changed && value_ >= max_) onHitMax.fire(activator, this);
        if (max_ > min_ && changed && value_ <= min_) onHitMin.fire(activator, this);
    }
    float value_ = 0, min_ = 0, max_ = 0;
};
LINK_ENTITY_TO_CLASS(math_counter, MathCounter)

// Shows "message" in the console (ShowMessage) or on screen (ShowHudMessage).
class PointMessage : public BaseEntity {
public:
    bool keyValue(const std::string& k, const std::string& v) override {
        if (nameEquals(k, "message")) { message_ = v; return true; }
        return BaseEntity::keyValue(k, v);
    }
    bool acceptInput(const std::string& in, BaseEntity* activator, BaseEntity* caller, const std::string& p) override {
        const std::string& text = p.empty() ? message_ : p;
        if (nameEquals(in, "ShowMessage")) {
            if (world().services().print) world().services().print(text);
            else log::info("{}", text);
            return true;
        }
        if (nameEquals(in, "ShowHudMessage")) {
            if (world().services().centerPrint) world().services().centerPrint(text);
            else log::info("{}", text);
            return true;
        }
        return BaseEntity::acceptInput(in, activator, caller, p);
    }

private:
    std::string message_;
};
LINK_ENTITY_TO_CLASS(point_message, PointMessage)
LINK_ENTITY_TO_CLASS(game_text, PointMessage)

} // namespace
// Referenced from game_world.cpp so the linker keeps this file (and its registrations).
void linkEntities_logic() {}

} // namespace k::game
