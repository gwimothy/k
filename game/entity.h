#pragma once
// Server entities (Source's CBaseEntity).
//
// Subclass BaseEntity, register the class with LINK_ENTITY_TO_CLASS, and handle:
//   keyValue()    map keys ("speed", "wait", ...) - return true if handled
//   spawn()       after all keys are set
//   think()       when setNextThink() time arrives
//   touch()       a player entered a trigger volume
//   use()         the player pressed +use on it
//   acceptInput() an I/O input ("Open", "Trigger", ...) - return true if handled
//
// Outputs ("OnPressed", "OnTrigger", ...) are declared as `Output` members and registered
// with declareOutput() so map keys can connect them.

#include "entity_io.h"
#include "shared/bspfile.h"
#include "shared/protocol.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace k::game {

class GameWorld;

enum class Solid {
    Not,      // no collision
    Bsp,      // brush model: blocks movement
    Trigger,  // brush model volume: reports touches, doesn't block
};

enum EntityEffects : uint32_t {
    EF_NODRAW = 1u << 0,
};

class BaseEntity {
public:
    virtual ~BaseEntity() = default;

    // ---- Lifecycle (override these) -------------------------------------------------
    virtual bool keyValue(const std::string& key, const std::string& value);
    virtual void spawn() {}
    virtual void activate() {} // all map entities have spawned
    virtual void think() {}
    virtual void startTouch(BaseEntity* /*other*/) {}
    virtual void endTouch(BaseEntity* /*other*/) {}
    virtual void use(BaseEntity* /*activator*/) {}
    virtual bool acceptInput(const std::string& input, BaseEntity* activator, BaseEntity* caller,
                             const std::string& param);
    // Called when a mover can't move because something is in the way.
    virtual void blocked(BaseEntity* /*other*/) {}

    // ---- State ------------------------------------------------------------------------
    int index = 0;
    std::string classname;
    std::string targetname;
    Vec3 origin{0};
    QAngle angles;
    int brushModel = 0;            // > 0 for brush entities ("model" "*N")
    std::string modelPath;         // for model entities (prop_dynamic)
    Solid solid = Solid::Not;
    uint32_t effects = 0;
    bool networked = false;        // included in snapshots
    bool markedForDeletion = false;

    // World-space bounds (brush entities: model bounds moved by origin).
    Vec3 absMins() const;
    Vec3 absMaxs() const;

    void setNextThink(double time) { nextThink_ = time; }
    double nextThink() const { return nextThink_; }
    GameWorld& world() const { return *world_; }

    // Outputs
    void declareOutput(const std::string& name, Output* output);
    Output* findOutput(const std::string& name);
    const std::map<std::string, Output*, NameLess>& outputs() const { return outputs_; }

private:
    friend class GameWorld;
    GameWorld* world_ = nullptr;
    double nextThink_ = 0;
    std::map<std::string, Output*, NameLess> outputs_;
};

// ---- Factory -------------------------------------------------------------------------

using EntityFactory = std::function<std::unique_ptr<BaseEntity>()>;

void registerEntityClass(const std::string& classname, EntityFactory factory);
std::unique_ptr<BaseEntity> createEntity(const std::string& classname);
std::vector<std::string> registeredEntityClasses();

#define LINK_ENTITY_TO_CLASS(mapname, Class)                                                    \
    namespace {                                                                                 \
    struct Register_##Class##_##mapname {                                                       \
        Register_##Class##_##mapname() {                                                        \
            ::k::game::registerEntityClass(#mapname, [] { return std::make_unique<Class>(); }); \
        }                                                                                       \
    } register_##Class##_##mapname##_instance;                                                  \
    }

} // namespace k::game
