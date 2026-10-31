// Simple entities: spawn points, targets, static brushes, models, and map-only classes.

#include "entity.h"
#include "game_world.h"

#include <cstdlib>

namespace k::game {
namespace {

// Point entities with no server behaviour (compile-time only, or just a named position).
class PointEntity : public BaseEntity {};
LINK_ENTITY_TO_CLASS(info_player_start, PointEntity)
LINK_ENTITY_TO_CLASS(info_target, PointEntity)
LINK_ENTITY_TO_CLASS(info_teleport_destination, PointEntity)
LINK_ENTITY_TO_CLASS(light, PointEntity)
LINK_ENTITY_TO_CLASS(light_environment, PointEntity)
LINK_ENTITY_TO_CLASS(prop_static, PointEntity) // drawn by the client straight from the map

// A brush entity that can be turned on and off (Source's func_brush). func_wall is the
// classic always-solid version; func_illusionary is visible but never solid.
class FuncBrush : public BaseEntity {
public:
    explicit FuncBrush(bool solidByDefault = true) : solidByDefault_(solidByDefault) {}
    bool keyValue(const std::string& k, const std::string& v) override {
        if (nameEquals(k, "StartDisabled")) { enabled_ = std::strtol(v.c_str(), nullptr, 10) == 0; return true; }
        if (nameEquals(k, "Solidity")) { solidity_ = static_cast<int>(std::strtol(v.c_str(), nullptr, 10)); return true; }
        return BaseEntity::keyValue(k, v);
    }
    void spawn() override {
        networked = true;
        apply();
    }
    bool acceptInput(const std::string& in, BaseEntity* activator, BaseEntity* caller, const std::string& p) override {
        if (nameEquals(in, "Enable")) { enabled_ = true; apply(); return true; }
        if (nameEquals(in, "Disable")) { enabled_ = false; apply(); return true; }
        if (nameEquals(in, "Toggle")) { enabled_ = !enabled_; apply(); return true; }
        return BaseEntity::acceptInput(in, activator, caller, p);
    }

private:
    void apply() {
        // Solidity: 0 = when visible, 1 = never, 2 = always (Source's values).
        bool isSolid = solidByDefault_ && (solidity_ == 2 || (solidity_ == 0 && enabled_));
        solid = isSolid ? Solid::Bsp : Solid::Not;
        if (enabled_) effects &= ~EF_NODRAW;
        else effects |= EF_NODRAW;
    }
    bool solidByDefault_;
    bool enabled_ = true;
    int solidity_ = 0;
};
LINK_ENTITY_TO_CLASS(func_brush, FuncBrush)
LINK_ENTITY_TO_CLASS(func_wall, FuncBrush)

class FuncIllusionary : public FuncBrush {
public:
    FuncIllusionary() : FuncBrush(false) {}
};
LINK_ENTITY_TO_CLASS(func_illusionary, FuncIllusionary)

// A model the server can show, hide and kill. No collision yet (Phase 6 adds physics).
class PropDynamic : public BaseEntity {
public:
    void spawn() override { networked = true; }
    bool acceptInput(const std::string& in, BaseEntity* activator, BaseEntity* caller, const std::string& p) override {
        if (nameEquals(in, "TurnOn") || nameEquals(in, "Enable")) { effects &= ~EF_NODRAW; return true; }
        if (nameEquals(in, "TurnOff") || nameEquals(in, "Disable")) { effects |= EF_NODRAW; return true; }
        return BaseEntity::acceptInput(in, activator, caller, p);
    }
};
LINK_ENTITY_TO_CLASS(prop_dynamic, PropDynamic)

} // namespace
// Referenced from game_world.cpp so the linker keeps this file (and its registrations).
void linkEntities_misc() {}

} // namespace k::game
