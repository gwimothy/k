#pragma once
// The game's world: every entity, the I/O event queue, the players, collision against
// moving brush entities and props, physics, and lag-compensated hitscan.
// Implements the engine-facing IGameServer.

#include "entity.h"
#include "game_server.h"
#include "physics.h"
#include "shared/gamemovement.h"

#include <array>
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

// A player. Movement state lives in a PlayerState so the shared movement code (and the
// client's prediction) can run on it.
class Player final : public BaseEntity {
public:
    int slot = -1;
    std::string name;
    proto::PlayerState state;
    uint32_t lastButtons = 0;
    bool teleported = false;    // the server forces the client's view angles next tick
    int heldProp = -1;
    float footstepTimer = 0;
    double respawnTime = 0;     // when dead
    std::vector<int> touching;  // trigger indices this player is inside
    Vec3 mins() const;
    Vec3 maxs() const;
};

class GameWorld final : public IGameServer, public TraceWorld {
public:
    explicit GameWorld(EngineServices services);
    ~GameWorld() override;

    // ---- IGameServer ------------------------------------------------------------------
    bool levelInit(const std::string& mapName, std::shared_ptr<const bsp::BspData> map) override;
    void levelShutdown() override;
    void clientConnect(int slot, const std::string& name) override;
    void clientDisconnect(int slot) override;
    void runCommand(int slot, const UserCmd& cmd, float dt) override;
    void clientCommand(int slot, const std::string& text) override;
    void frame(float dt, uint32_t tick) override;
    const proto::PlayerState& playerState(int slot) const override;
    QAngle spawnAngles(int slot) const override;
    void setPlayerOrigin(int slot, const Vec3& origin) override;
    const std::vector<std::string>& modelTable() const override { return modelTable_; }
    void buildEntityStates(int slot, std::vector<proto::EntityState>& out) const override;
    double time() const override { return time_; }
    bool takeAngleOverride(int slot, QAngle& angles) override;

    // ---- TraceWorld (world brushes + brush entities + props + other players) ------------
    bsp::TraceResult traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                              uint32_t mask) const override;

    // ---- For entities --------------------------------------------------------------------
    const bsp::BspData* map() const { return map_.get(); }
    EventQueue& events() { return events_; }
    BaseEntity* entity(int index) const;
    std::vector<BaseEntity*> findByName(const std::string& pattern) const;
    BaseEntity* findFirstByName(const std::string& name) const;
    Player* player(int slot = 0) const;
    std::vector<Player*> players() const;
    PhysicsWorld* physics() const { return physics_.get(); }
    const EngineServices& services() const { return services_; }

    // Spawn an entity from key/values (used by map loading and by tests).
    BaseEntity* spawnEntity(const bsp::Entity& keys);

    // Move a pusher (door, elevator) by `delta`, carrying or pushing players.
    // Returns false (and doesn't move) if a player is in the way and can't be moved.
    bool tryPush(BaseEntity& mover, const Vec3& delta);

    // Play a sound script at an entity (follows it) or at a position (entity = nullptr).
    void emitSound(const std::string& name, const BaseEntity* entity, const Vec3& origin, float volume = 1.0f);
    void stopSound(const std::string& name, const BaseEntity* entity);

    // Deliver an input right now (the event queue calls this).
    void deliverInput(const std::string& target, const std::string& input, const std::string& param,
                      BaseEntity* activator, BaseEntity* caller);

    // A hitscan shot from `shooter`, with other players rewound to `viewTick` (lag
    // compensation). Returns the entity index hit (0 = world, -1 = nothing).
    int fireHitscan(Player& shooter, uint32_t viewTick, Vec3* hitPos = nullptr);

    // Hurt a player. `attacker` may be null (world, trigger_hurt, falling).
    void damagePlayer(Player& victim, int amount, Player* attacker, const std::string& how);
    void respawn(Player& p);

private:
    void touchTriggers(Player& p);
    void handleUse(Player& p);
    void updateHeldProp(Player& p, const UserCmd& cmd, float dt);
    void pushTouchedProps(Player& p, const Vec3& playerVelocity);
    void playerSounds(Player& p, const proto::PlayerState& before, float dt);
    void propImpactSounds();
    int addEntity(std::unique_ptr<BaseEntity> e);
    void buildModelTable();
    bool playerOverlaps(const Player& p, const BaseEntity& e, const Vec3& playerOrigin) const;
    void recordHistory(uint32_t tick);

    EngineServices services_;
    std::shared_ptr<const bsp::BspData> map_;
    std::vector<std::unique_ptr<BaseEntity>> entities_;
    std::array<Player*, proto::kMaxClients> players_{};
    std::vector<std::string> modelTable_;
    EventQueue events_;
    std::unique_ptr<PhysicsWorld> physics_;
    mutable std::vector<int> moveTouches_; // props hit by the current player's movement traces
    const Player* moving_ = nullptr;       // the player whose movement is being traced (not an obstacle)
    Vec3 spawnOrigin_{0};
    QAngle spawnAngles_;
    double time_ = 0;
    uint32_t tick_ = 0;
    struct PropMotion {
        Vec3 velocity{0};
        double lastImpact = -1;
    };
    std::vector<PropMotion> propMotion_; // by entity index
    // Lag compensation: player origins for the last second of ticks.
    struct HistoryFrame {
        uint32_t tick = 0;
        std::array<Vec3, proto::kMaxClients> origins{};
        std::array<bool, proto::kMaxClients> ducking{};
        std::array<bool, proto::kMaxClients> present{};
    };
    std::deque<HistoryFrame> history_;
};

inline const char* kPlayerModel = "models/player/placeholder.glb";

} // namespace k::game
