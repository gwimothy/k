#pragma once
// The game's world: every entity, the I/O event queue, the player, and collision against
// moving brush entities. Implements the engine-facing IGameServer.

#include "entity.h"
#include "game_server.h"
#include "shared/gamemovement.h"

#include <deque>
#include <memory>
#include <vector>

namespace k::game {

class Player;

class EventQueue {
public:
    void add(const Connection& c, const std::string& param, BaseEntity* activator, BaseEntity* caller);
    // Deliver every event whose time has come.
    void service(GameWorld& world);
    void clear() { events_.clear(); }
    size_t size() const { return events_.size(); }

private:
    struct Event {
        double time;
        std::string target, input, param;
        int activator, caller; // entity indices (entities may be deleted before delivery)
    };
    std::deque<Event> events_;
    GameWorld* world_ = nullptr;
    friend class GameWorld;
};

class GameWorld final : public IGameServer, public TraceWorld {
public:
    explicit GameWorld(EngineServices services);
    ~GameWorld() override;

    // ---- IGameServer ------------------------------------------------------------------
    bool levelInit(const std::string& mapName, std::shared_ptr<const bsp::BspData> map) override;
    void levelShutdown() override;
    void clientPutInServer(const std::string& name) override;
    void runCommand(const UserCmd& cmd, float dt) override;
    void clientCommand(const std::string& text) override;
    void frame(float dt) override;
    const proto::PlayerState& playerState() const override;
    QAngle spawnAngles() const override { return spawnAngles_; }
    void setPlayerOrigin(const Vec3& origin) override;
    const std::vector<std::string>& modelTable() const override { return modelTable_; }
    void buildEntityStates(std::vector<proto::EntityState>& out) const override;
    double time() const override { return time_; }
    bool takeAngleOverride(QAngle& angles) override;

    // ---- TraceWorld (world brushes + solid brush entities) ------------------------------
    bsp::TraceResult traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                              uint32_t mask) const override;

    // ---- For entities --------------------------------------------------------------------
    const bsp::BspData* map() const { return map_.get(); }
    EventQueue& events() { return events_; }
    BaseEntity* entity(int index) const;
    std::vector<BaseEntity*> findByName(const std::string& pattern) const;
    BaseEntity* findFirstByName(const std::string& name) const;
    Player* player() const { return player_; }
    PhysicsWorld* physics() const { return physics_.get(); }
    const EngineServices& services() const { return services_; }

    // Spawn an entity from key/values (used by map loading and by tests).
    BaseEntity* spawnEntity(const bsp::Entity& keys);

    // Move a pusher (door, elevator) by `delta`, carrying or pushing the player.
    // Returns false (and doesn't move) if the player is in the way and can't be moved.
    bool tryPush(BaseEntity& mover, const Vec3& delta);

    // Play a sound script at an entity (follows it) or at a position (entity = nullptr).
    void emitSound(const std::string& name, const BaseEntity* entity, const Vec3& origin, float volume = 1.0f);
    void stopSound(const std::string& name, const BaseEntity* entity);

    // Deliver an input right now (the event queue calls this).
    void deliverInput(const std::string& target, const std::string& input, const std::string& param,
                      BaseEntity* activator, BaseEntity* caller);

private:
    void touchTriggers();
    void handleUse();
    void updateHeldProp(const UserCmd& cmd, float dt);
    void pushTouchedProps(const Vec3& playerVelocity);
    void playerSounds(const proto::PlayerState& before, float dt);
    void propImpactSounds();
    int addEntity(std::unique_ptr<BaseEntity> e);
    void buildModelTable();
    bool playerOverlaps(const BaseEntity& e, const Vec3& playerOrigin) const;

    EngineServices services_;
    std::shared_ptr<const bsp::BspData> map_;
    std::vector<std::unique_ptr<BaseEntity>> entities_;
    std::vector<int> touching_; // trigger indices the player is inside
    std::vector<std::string> modelTable_;
    EventQueue events_;
    std::unique_ptr<PhysicsWorld> physics_;
    mutable std::vector<int> moveTouches_; // props hit by the player's movement traces this command
    int heldProp_ = -1;
    float footstepTimer_ = 0;
    struct PropMotion {
        Vec3 velocity{0};
        double lastImpact = -1;
    };
    std::vector<PropMotion> propMotion_; // by entity index
    Player* player_ = nullptr;
    QAngle spawnAngles_;
    double time_ = 0;
};

// The player entity. Movement state lives in a PlayerState so the shared movement code
// (and later client prediction) can run on it.
class Player final : public BaseEntity {
public:
    proto::PlayerState state;
    uint32_t lastButtons = 0;
    std::string name;
    bool teleported = false; // set by teleporters; the server forces the client's view angles
    Vec3 mins() const;
    Vec3 maxs() const;
};

} // namespace k::game
