#include "game_world.h"

#include "core/cvar.h"
#include "core/keyvalues.h"
#include "core/log.h"
#include "shared/bsp_trace.h"

#include <algorithm>
#include <cmath>

namespace k::game {
namespace {

// Movement settings (replicated so client prediction matches the server).
ConVar sv_maxspeed("sv_maxspeed", "320", FCVAR_REPLICATED | FCVAR_NOTIFY, "Maximum player speed.");
ConVar sv_accelerate("sv_accelerate", "10", FCVAR_REPLICATED | FCVAR_NOTIFY, "Ground acceleration.");
ConVar sv_airaccelerate("sv_airaccelerate", "10", FCVAR_REPLICATED | FCVAR_NOTIFY, "Air acceleration.");
ConVar sv_friction("sv_friction", "4", FCVAR_REPLICATED | FCVAR_NOTIFY, "World friction.");
ConVar sv_stopspeed("sv_stopspeed", "100", FCVAR_REPLICATED | FCVAR_NOTIFY, "Minimum stopping speed on ground.");
ConVar sv_gravity("sv_gravity", "800", FCVAR_REPLICATED | FCVAR_NOTIFY, "World gravity.");
ConVar sv_stepsize("sv_stepsize", "18", FCVAR_REPLICATED | FCVAR_NOTIFY, "Maximum step-up height.");
ConVar sv_autobunnyhopping("sv_autobunnyhopping", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "Holding jump re-jumps on landing.");
ConVar sv_noclipspeed("sv_noclipspeed", "5", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Noclip speed multiplier.");
ConVar sv_noclipaccelerate("sv_noclipaccelerate", "5", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Noclip acceleration.");
ConVar sv_lagcompensation("sv_lagcompensation", "1", FCVAR_NONE, "Rewind players to what the shooter saw.");
ConVar sv_hitscan_damage("sv_hitscan_damage", "25", FCVAR_NOTIFY, "Damage per hitscan hit.", 0.0f, 1000.0f);
ConVar sv_falldamage("sv_falldamage", "1", FCVAR_NOTIFY, "Players take damage from long falls.");
ConVar sv_respawntime("sv_respawntime", "3", FCVAR_NOTIFY, "Seconds before a dead player respawns.", 0.0f, 60.0f);
ConVar developer_io("developer_io", "0", FCVAR_NONE, "Log every entity I/O event as it fires.");

constexpr float kUseDistance = 72.0f;
constexpr float kHitscanRange = 8192.0f;
constexpr double kHistorySeconds = 1.0;

bool boxesOverlap(const Vec3& amin, const Vec3& amax, const Vec3& bmin, const Vec3& bmax) {
    return amin.x < bmax.x && amax.x > bmin.x && amin.y < bmax.y && amax.y > bmin.y && amin.z < bmax.z && amax.z > bmin.z;
}

// Ray vs AABB (slab test). Returns the entry distance along `dir` (unit or not) or -1.
float rayBox(const Vec3& o, const Vec3& dir, const Vec3& mn, const Vec3& mx) {
    float tmin = 0, tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dir[i]) < 1e-8f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return -1;
            continue;
        }
        float t1 = (mn[i] - o[i]) / dir[i], t2 = (mx[i] - o[i]) / dir[i];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return -1;
    }
    return tmin;
}

// Swept box against an axis-aligned box. Returns fraction and fills the normal, or -1.
// Starting inside counts as a miss (so overlapping objects never trap each other).
float sweepBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs, const Vec3& boxMin,
               const Vec3& boxMax, Vec3& normal) {
    Vec3 bmin = boxMin - maxs, bmax = boxMax - mins; // Minkowski-expanded
    Vec3 dir = end - start;
    float tEnter = 0, tExit = 1;
    int axis = -1;
    bool inside = true;
    for (int i = 0; i < 3; ++i) {
        if (start[i] < bmin[i] || start[i] > bmax[i]) inside = false;
        if (std::fabs(dir[i]) < 1e-7f) {
            if (start[i] < bmin[i] || start[i] > bmax[i]) return -1;
            continue;
        }
        float t1 = (bmin[i] - start[i]) / dir[i], t2 = (bmax[i] - start[i]) / dir[i];
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tEnter) { tEnter = t1; axis = i; }
        tExit = std::min(tExit, t2);
        if (tEnter > tExit) return -1;
    }
    if (inside || axis < 0) return -1;
    float len = glm::length(dir);
    normal = Vec3(0);
    normal[axis] = dir[axis] > 0 ? -1.0f : 1.0f;
    return std::max(0.0f, tEnter - (len > 0 ? bsp::kDistEpsilon / len : 0.0f));
}

} // namespace

Vec3 Player::mins() const { return (state.flags & FL_DUCKING) ? kDuckHullMins : kHullMins; }
Vec3 Player::maxs() const { return (state.flags & FL_DUCKING) ? kDuckHullMaxs : kHullMaxs; }

// ---- EventQueue -------------------------------------------------------------------------

void EventQueue::add(const Connection& c, const std::string& param, BaseEntity* activator, BaseEntity* caller) {
    double now = world_ ? world_->time() : 0.0;
    events_.push_back({now + c.delay, c.target, c.input, param, activator ? activator->index : -1,
                       caller ? caller->index : -1});
}

void EventQueue::service(GameWorld& world) {
    // Events added while servicing (zero-delay chains) run in the same frame, with a cap
    // so a loop like relay -> itself can't hang the server.
    int budget = 4096;
    while (budget-- > 0) {
        auto it = std::find_if(events_.begin(), events_.end(), [&](const Event& e) { return e.time <= world.time(); });
        if (it == events_.end()) return;
        Event e = std::move(*it);
        events_.erase(it);
        world.deliverInput(e.target, e.input, e.param, world.entity(e.activator), world.entity(e.caller));
    }
    log::warn("Entity I/O: too many events in one frame (output loop?)");
}

// ---- GameWorld ----------------------------------------------------------------------------

// Entity classes register themselves with static objects; referencing one function per file
// stops the linker from discarding those files from the static library.
void linkEntities_logic();
void linkEntities_triggers();
void linkEntities_movers();
void linkEntities_misc();
void linkEntities_props();

GameWorld::GameWorld(EngineServices services) : services_(std::move(services)) {
    events_.world_ = this;
    linkEntities_logic();
    linkEntities_triggers();
    linkEntities_movers();
    linkEntities_misc();
    linkEntities_props();
}

GameWorld::~GameWorld() { levelShutdown(); }

std::unique_ptr<IGameServer> createGameServer(EngineServices services) {
    return std::make_unique<GameWorld>(std::move(services));
}

int GameWorld::addEntity(std::unique_ptr<BaseEntity> e) {
    // Reuse free slots so indices stay small.
    for (size_t i = 1; i < entities_.size(); ++i) {
        if (!entities_[i]) {
            e->index = static_cast<int>(i);
            e->world_ = this;
            entities_[i] = std::move(e);
            return static_cast<int>(i);
        }
    }
    e->index = static_cast<int>(entities_.size());
    e->world_ = this;
    entities_.push_back(std::move(e));
    return entities_.back()->index;
}

BaseEntity* GameWorld::entity(int index) const {
    return (index >= 0 && static_cast<size_t>(index) < entities_.size()) ? entities_[static_cast<size_t>(index)].get() : nullptr;
}

Player* GameWorld::player(int slot) const {
    return (slot >= 0 && slot < proto::kMaxClients) ? players_[static_cast<size_t>(slot)] : nullptr;
}

std::vector<Player*> GameWorld::players() const {
    std::vector<Player*> out;
    for (auto* p : players_)
        if (p) out.push_back(p);
    return out;
}

std::vector<BaseEntity*> GameWorld::findByName(const std::string& pattern) const {
    std::vector<BaseEntity*> out;
    for (auto& e : entities_)
        if (e && !e->targetname.empty() && nameMatches(pattern, e->targetname)) out.push_back(e.get());
    return out;
}

BaseEntity* GameWorld::findFirstByName(const std::string& name) const {
    auto all = findByName(name);
    return all.empty() ? nullptr : all.front();
}

BaseEntity* GameWorld::spawnEntity(const bsp::Entity& keys) {
    std::string cls = bsp::entityString(keys, "classname");
    auto e = createEntity(cls);
    if (!e) {
        log::debug("Unknown entity class '{}' skipped", cls);
        return nullptr;
    }
    BaseEntity* raw = e.get();
    addEntity(std::move(e));
    for (auto& [k, v] : keys) {
        if (k == "classname") continue;
        if (!raw->keyValue(k, v)) log::debug("{}: unused key '{}'", cls, k);
    }
    raw->spawn();
    // Solid brush entities get a kinematic physics body so props collide with doors and lifts.
    if (physics_ && map_ && raw->solid == Solid::Bsp && raw->brushModel > 0 && !raw->physicsBody.valid())
        raw->physicsBody = physics_->addBrushModel(*map_, raw->brushModel, raw->origin);
    return raw;
}

bool GameWorld::levelInit(const std::string& /*mapName*/, std::shared_ptr<const bsp::BspData> map) {
    levelShutdown();
    map_ = std::move(map);
    time_ = 0;
    physics_ = std::make_unique<PhysicsWorld>();
    if (map_) physics_->loadWorld(*map_);

    // Slot 0 is the world.
    auto worldEnt = std::make_unique<BaseEntity>();
    worldEnt->classname = "worldspawn";
    worldEnt->world_ = this;
    entities_.push_back(std::move(worldEnt));

    if (map_) {
        for (size_t i = 1; i < map_->entities.size(); ++i) spawnEntity(map_->entities[i]);
        if (auto* start = map_->findEntity("info_player_start")) {
            Vec3 a = bsp::entityVec3(*start, "angles");
            spawnAngles_ = {a.x, a.y, a.z};
            spawnOrigin_ = bsp::entityVec3(*start, "origin");
        } else {
            log::warn("Map has no info_player_start; spawning at the origin");
        }
    }
    for (auto& e : entities_)
        if (e) e->activate();
    buildModelTable();
    return true;
}

void GameWorld::levelShutdown() {
    events_.clear();
    players_.fill(nullptr);
    entities_.clear(); // before physics_: entities may own bodies
    physics_.reset();
    modelTable_.clear();
    history_.clear();
    propMotion_.clear();
    map_.reset();
}

void GameWorld::buildModelTable() {
    modelTable_.assign(1, ""); // 0 = no model
    modelTable_.push_back(kPlayerModel);
    for (auto& e : entities_) {
        if (!e || !e->networked) continue;
        std::string name = e->brushModel > 0 ? "*" + std::to_string(e->brushModel) : e->modelPath;
        if (!name.empty() && std::find(modelTable_.begin(), modelTable_.end(), name) == modelTable_.end())
            modelTable_.push_back(name);
    }
}

void GameWorld::clientConnect(int slot, const std::string& name) {
    if (slot < 0 || slot >= proto::kMaxClients) return;
    clientDisconnect(slot);
    auto p = std::make_unique<Player>();
    p->classname = "player";
    p->slot = slot;
    p->name = name;
    p->networked = true;
    p->modelPath = kPlayerModel;
    // Spread players out a little so they don't spawn inside each other.
    Vec3 offset(0);
    int others = static_cast<int>(players().size());
    if (others > 0) offset = Vec3(std::cos(others * 1.3f), std::sin(others * 1.3f), 0) * 48.0f;
    p->state.origin = spawnOrigin_ + offset;
    p->state.viewAngles = spawnAngles_;
    p->origin = p->state.origin;
    Player* raw = p.get();
    addEntity(std::move(p));
    players_[static_cast<size_t>(slot)] = raw;
}

void GameWorld::clientDisconnect(int slot) {
    Player* p = player(slot);
    if (!p) return;
    for (int t : p->touching)
        if (auto* e = entity(t)) e->endTouch(p);
    p->markedForDeletion = true;
    players_[static_cast<size_t>(slot)] = nullptr;
    // Remove right away so the slot can be reused this tick.
    for (auto& e : entities_)
        if (e.get() == p) e.reset();
}

void GameWorld::buildEntityStates(int slot, std::vector<proto::EntityState>& out) const {
    out.clear();
    const Player* self = player(slot);
    for (auto& e : entities_) {
        if (!e || !e->networked || e.get() == self) continue;
        std::string name = e->brushModel > 0 ? "*" + std::to_string(e->brushModel) : e->modelPath;
        auto it = std::find(modelTable_.begin(), modelTable_.end(), name);
        proto::EntityState s;
        s.index = static_cast<uint16_t>(e->index);
        s.model = static_cast<uint16_t>(it == modelTable_.end() ? 0 : it - modelTable_.begin());
        s.origin = e->origin;
        s.angles = e->angles;
        if (auto* p = dynamic_cast<const Player*>(e.get())) s.angles = {0, p->state.viewAngles.yaw, 0};
        s.effects = e->effects;
        out.push_back(s);
    }
}

const proto::PlayerState& GameWorld::playerState(int slot) const {
    static const proto::PlayerState empty;
    const Player* p = player(slot);
    return p ? p->state : empty;
}

QAngle GameWorld::spawnAngles(int slot) const {
    const Player* p = player(slot);
    return p ? p->state.viewAngles : spawnAngles_;
}

void GameWorld::setPlayerOrigin(int slot, const Vec3& origin) {
    Player* p = player(slot);
    if (!p) return;
    p->state.origin = origin;
    p->state.velocity = Vec3(0);
    p->origin = origin;
}

bool GameWorld::takeAngleOverride(int slot, QAngle& angles) {
    Player* p = player(slot);
    if (!p || !p->teleported) return false;
    p->teleported = false;
    angles = p->state.viewAngles;
    return true;
}

bsp::TraceResult GameWorld::traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                                     uint32_t mask) const {
    bsp::TraceResult best;
    best.endPos = end;
    if (map_) best = bsp::traceBox(*map_, start, end, mins, maxs, mask);
    if (best.allSolid || !map_) return best;

    auto consider = [&](float frac, const Vec3& normal, int entity) {
        if (frac < 0 || frac >= best.fraction) return false;
        best.fraction = frac;
        best.endPos = start + (end - start) * frac;
        best.normal = normal;
        best.entity = entity;
        best.contents = bsp::CONTENTS_SOLID;
        return true;
    };

    // Physics props and other players block like boxes.
    int movingHeld = moving_ ? moving_->heldProp : -1;
    for (auto& e : entities_) {
        if (!e || e.get() == moving_) continue;
        Vec3 normal;
        if (e->physicsDriven && e->index != movingHeld) {
            if (consider(sweepBox(start, end, mins, maxs, e->absMins(), e->absMaxs(), normal), normal, e->index))
                moveTouches_.push_back(e->index);
        } else if (auto* p = dynamic_cast<const Player*>(e.get())) {
            if (p->state.dead || p->state.moveType == MOVETYPE_NOCLIP || (moving_ && moving_->state.moveType == MOVETYPE_NOCLIP)) continue;
            consider(sweepBox(start, end, mins, maxs, p->state.origin + p->mins(), p->state.origin + p->maxs(), normal),
                     normal, e->index);
        }
    }
    // Solid brush entities.
    for (auto& e : entities_) {
        if (!e || e->solid != Solid::Bsp || e->brushModel <= 0) continue;
        Vec3 smin = glm::min(start, end) + mins - Vec3(1), smax = glm::max(start, end) + maxs + Vec3(1);
        if (!boxesOverlap(smin, smax, e->absMins(), e->absMaxs())) continue;
        auto tr = bsp::traceBoxModel(*map_, e->brushModel, e->origin, start, end, mins, maxs, mask);
        if (tr.startSolid && !best.startSolid) {
            best.startSolid = true;
            best.entity = e->index;
        }
        if (tr.allSolid || tr.fraction < best.fraction) {
            bool startSolid = best.startSolid;
            best = tr;
            best.startSolid = best.startSolid || startSolid;
            best.entity = e->index;
            if (tr.allSolid) return best;
        }
    }
    return best;
}

void GameWorld::damagePlayer(Player& victim, int amount, Player* attacker, const std::string& how) {
    if (victim.state.dead || amount <= 0) return;
    if (victim.state.moveType == MOVETYPE_NOCLIP && !attacker) return; // noclip ignores the world
    victim.state.health = static_cast<int16_t>(std::max(0, victim.state.health - amount));
    if (victim.state.health > 0) {
        emitSound("Player.Pain", &victim, victim.origin);
        return;
    }
    victim.state.dead = 1;
    victim.state.velocity = Vec3(0);
    victim.state.flags &= ~FL_DUCKING;
    victim.effects |= EF_NODRAW;
    victim.heldProp = -1;
    victim.respawnTime = time_ + sv_respawntime.getFloat();
    emitSound("Player.Death", nullptr, victim.origin);
    std::string killer = attacker && attacker != &victim ? attacker->name : "";
    log::info("{} {} {}", killer.empty() ? victim.name : killer, killer.empty() ? "died" : "killed", killer.empty() ? "(" + how + ")" : victim.name);
    if (services_.killFeed) services_.killFeed(killer, victim.name, how);
}

void GameWorld::respawn(Player& p) {
    p.state = proto::PlayerState{};
    Vec3 offset(0);
    int n = static_cast<int>(players().size());
    if (n > 1) offset = Vec3(std::cos(p.slot * 1.3f), std::sin(p.slot * 1.3f), 0) * 48.0f;
    p.state.origin = spawnOrigin_ + offset;
    p.state.viewAngles = spawnAngles_;
    p.origin = p.state.origin;
    p.effects &= ~EF_NODRAW;
    p.teleported = true; // snap the client's view to the spawn angles
    for (int t : p.touching)
        if (auto* e = entity(t)) e->endTouch(&p);
    p.touching.clear();
}

void GameWorld::runCommand(int slot, const UserCmd& cmd, float dt) {
    Player* p = player(slot);
    if (!p) return;
    if (p->state.dead) {
        // Dead: no movement. Respawn when the timer runs out (or on +jump/+attack after it).
        if (time_ >= p->respawnTime) respawn(*p);
        p->lastButtons = cmd.buttons;
        return;
    }
    MoveParams params;
    params.maxSpeed = sv_maxspeed.getFloat();
    params.accelerate = sv_accelerate.getFloat();
    params.airAccelerate = sv_airaccelerate.getFloat();
    params.friction = sv_friction.getFloat();
    params.stopSpeed = sv_stopspeed.getFloat();
    params.gravity = sv_gravity.getFloat();
    params.stepSize = sv_stepsize.getFloat();
    params.jumpImpulse = std::sqrt(2.0f * params.gravity * 57.0f);
    params.autoBunnyhop = sv_autobunnyhopping.getBool();
    params.noclipSpeed = sv_noclipspeed.getFloat();
    params.noclipAccelerate = sv_noclipaccelerate.getFloat();

    moveTouches_.clear();
    Vec3 velocityBefore = p->state.velocity;
    proto::PlayerState stateBefore = p->state;
    moving_ = p;
    playerMove(p->state, cmd, dt, params, map_ ? static_cast<const TraceWorld*>(this) : nullptr);
    moving_ = nullptr;
    p->origin = p->state.origin;
    p->angles = p->state.viewAngles;
    pushTouchedProps(*p, velocityBefore);
    playerSounds(*p, stateBefore, dt);
    // Fall damage, Source's formula: safe below ~526 u/s, fatal around 922 u/s.
    bool landed = !(stateBefore.flags & FL_ONGROUND) && (p->state.flags & FL_ONGROUND);
    if (landed && sv_falldamage.getBool() && p->state.moveType != MOVETYPE_NOCLIP && stateBefore.velocity.z < -526.5f) {
        float speed = -stateBefore.velocity.z;
        damagePlayer(*p, static_cast<int>((speed - 526.5f) * (100.0f / (922.5f - 526.5f))), nullptr, "fell");
    }

    bool usePressed = (cmd.buttons & IN_USE) && !(p->lastButtons & IN_USE);
    bool attackPressed = (cmd.buttons & IN_ATTACK) && !(p->lastButtons & IN_ATTACK);
    if (usePressed) handleUse(*p);
    if (attackPressed && p->heldProp < 0) {
        Vec3 hit;
        int what = fireHitscan(*p, cmd.tickCount, &hit);
        emitSound("Weapon.Fire", p, p->origin);
        if (auto* target = dynamic_cast<Player*>(entity(what))) {
            emitSound("Player.Hit", target, target->origin);
            if (services_.hitConfirm) services_.hitConfirm(p->slot);
            damagePlayer(*target, sv_hitscan_damage.getInt(), p, "hitscan");
        }
    }
    updateHeldProp(*p, cmd, dt);
    p->lastButtons = cmd.buttons;
    touchTriggers(*p);
}

void GameWorld::recordHistory(uint32_t tick) {
    HistoryFrame f;
    f.tick = tick;
    for (size_t i = 0; i < players_.size(); ++i) {
        if (!players_[i]) continue;
        f.present[i] = true;
        f.origins[i] = players_[i]->state.origin;
        f.ducking[i] = (players_[i]->state.flags & FL_DUCKING) != 0;
    }
    history_.push_back(f);
    while (!history_.empty() && history_.size() > static_cast<size_t>(kHistorySeconds * 128)) history_.pop_front();
}

int GameWorld::fireHitscan(Player& shooter, uint32_t viewTick, Vec3* hitPos) {
    Vec3 eye = shooter.state.origin + Vec3(0, 0, shooter.state.viewHeight);
    Vec3 forward;
    angleVectors(shooter.state.viewAngles, &forward);
    Vec3 end = eye + forward * kHitscanRange;
    float wallFrac = map_ ? bsp::traceLine(*map_, eye, end).fraction : 1.0f;
    float bestDist = wallFrac * kHitscanRange;
    int hit = wallFrac < 1.0f ? 0 : -1;

    // Where were the other players when the shooter saw them? Use the recorded frame for
    // the tick the client was rendering (it interpolates entities in the past).
    const HistoryFrame* frame = nullptr;
    if (sv_lagcompensation.getBool() && viewTick > 0)
        for (auto it = history_.rbegin(); it != history_.rend(); ++it)
            if (it->tick <= viewTick) { frame = &*it; break; }

    for (auto* target : players()) {
        if (target == &shooter || target->state.dead) continue;
        Vec3 origin = target->state.origin;
        bool ducking = (target->state.flags & FL_DUCKING) != 0;
        if (frame && frame->present[static_cast<size_t>(target->slot)]) {
            origin = frame->origins[static_cast<size_t>(target->slot)];
            ducking = frame->ducking[static_cast<size_t>(target->slot)];
        }
        Vec3 mn = origin + (ducking ? kDuckHullMins : kHullMins), mx = origin + (ducking ? kDuckHullMaxs : kHullMaxs);
        float d = rayBox(eye, forward, mn, mx);
        if (d >= 0 && d < bestDist) {
            bestDist = d;
            hit = target->index;
        }
    }
    for (auto& e : entities_) {
        if (!e || !e->physicsDriven || e->index == shooter.heldProp) continue;
        float d = rayBox(eye, forward, e->absMins(), e->absMaxs());
        if (d >= 0 && d < bestDist) {
            bestDist = d;
            hit = e->index;
        }
    }
    if (auto* prop = entity(hit); prop && prop->physicsDriven) {
        physics_->wake(prop->physicsBody);
        physics_->addImpulse(prop->physicsBody, forward * 300.0f * std::min(physics_->mass(prop->physicsBody), 40.0f));
    }
    Vec3 pos = eye + forward * bestDist;
    if (hitPos) *hitPos = pos;
    if (services_.tempEntity && hit >= 0) {
        if (hit == 0 && map_) {
            // World: the trace gives the surface normal for the decal.
            auto tr = bsp::traceLine(*map_, eye, end);
            services_.tempEntity(static_cast<uint8_t>(proto::TempEntityType::Impact), tr.endPos, tr.normal);
        } else if (dynamic_cast<Player*>(entity(hit))) {
            services_.tempEntity(static_cast<uint8_t>(proto::TempEntityType::Blood), pos, -forward);
        } else {
            services_.tempEntity(static_cast<uint8_t>(proto::TempEntityType::Spark), pos, -forward);
        }
    }
    return hit;
}

void GameWorld::handleUse(Player& p) {
    Vec3 eye = p.state.origin + Vec3(0, 0, p.state.viewHeight);
    Vec3 forward;
    angleVectors(p.state.viewAngles, &forward);
    // Nearest usable brush entity along the view ray, not behind a wall.
    float wallDist = kUseDistance;
    if (map_) wallDist = bsp::traceLine(*map_, eye, eye + forward * kUseDistance).fraction * kUseDistance;
    BaseEntity* best = nullptr;
    float bestDist = std::min(wallDist + 2.0f, kUseDistance);
    for (auto& e : entities_) {
        if (!e || e->brushModel <= 0) continue;
        float d = rayBox(eye, forward, e->absMins() - Vec3(2), e->absMaxs() + Vec3(2));
        if (d >= 0 && d <= bestDist) {
            bestDist = d;
            best = e.get();
        }
    }
    if (best) {
        best->use(&p);
        return;
    }
    if (p.heldProp >= 0) { // drop
        p.heldProp = -1;
        return;
    }
    // Pick up the nearest light prop in front, unless someone else is holding it.
    float propDist = std::min(wallDist + 8.0f, 96.0f);
    for (auto& e : entities_) {
        if (!e || !e->physicsDriven) continue;
        bool taken = false;
        for (auto* other : players()) taken |= other->heldProp == e->index;
        if (taken) continue;
        float d = rayBox(eye, forward, e->absMins(), e->absMaxs());
        if (d >= 0 && d <= propDist && physics_->mass(e->physicsBody) <= 60.0f) {
            propDist = d;
            p.heldProp = e->index;
        }
    }
}

void GameWorld::updateHeldProp(Player& p, const UserCmd& cmd, float dt) {
    BaseEntity* prop = entity(p.heldProp);
    if (!prop || !prop->physicsDriven) {
        p.heldProp = -1;
        return;
    }
    Vec3 eye = p.state.origin + Vec3(0, 0, p.state.viewHeight);
    Vec3 forward;
    angleVectors(p.state.viewAngles, &forward);
    Vec3 center = (prop->absMins() + prop->absMaxs()) * 0.5f;
    float radius = glm::length(prop->absMaxs() - prop->absMins()) * 0.5f;
    if (cmd.buttons & IN_ATTACK) {
        // Throw.
        physics_->setLinearVelocity(prop->physicsBody, forward * 700.0f + p.state.velocity);
        p.heldProp = -1;
        p.lastButtons |= IN_ATTACK; // don't also fire this tick
        return;
    }
    Vec3 target = eye + forward * (24.0f + radius);
    Vec3 toTarget = target - center;
    if (glm::length(toTarget) > 160.0f) { // snagged on something: let go
        p.heldProp = -1;
        return;
    }
    Vec3 vel = toTarget / std::max(dt, 1e-3f) * 0.4f + p.state.velocity;
    float speed = glm::length(vel);
    if (speed > 1200.0f) vel *= 1200.0f / speed;
    physics_->wake(prop->physicsBody);
    physics_->setLinearVelocity(prop->physicsBody, vel);
    physics_->setAngularVelocity(prop->physicsBody, Vec3(0));
}

void GameWorld::emitSound(const std::string& name, const BaseEntity* entity, const Vec3& origin, float volume) {
    if (!services_.emitSound || name.empty()) return;
    services_.emitSound(name, entity ? entity->index : 0, entity ? (entity->absMins() + entity->absMaxs()) * 0.5f : origin,
                        volume, false);
}

void GameWorld::stopSound(const std::string& name, const BaseEntity* entity) {
    if (services_.emitSound && entity) services_.emitSound(name, entity->index, entity->origin, 0.0f, true);
}

void GameWorld::playerSounds(Player& p, const proto::PlayerState& before, float dt) {
    const auto& s = p.state;
    if (s.moveType == MOVETYPE_NOCLIP) return;
    bool wasGround = before.flags & FL_ONGROUND, onGround = s.flags & FL_ONGROUND;
    if (wasGround && !onGround && s.velocity.z > 200.0f) emitSound("Player.Jump", nullptr, s.origin, 1.0f);
    if (!wasGround && onGround && before.velocity.z < -300.0f)
        emitSound("Player.Land", nullptr, s.origin, std::clamp(-before.velocity.z / 600.0f, 0.3f, 1.0f));
    // Footsteps: Source steps every ~0.3 s at run speed; quieter and slower when walking.
    float speed = glm::length(Vec2(s.velocity.x, s.velocity.y));
    if (onGround && speed > 90.0f && !(s.flags & FL_DUCKING)) {
        p.footstepTimer -= dt;
        if (p.footstepTimer <= 0) {
            emitSound("Player.Footstep", nullptr, s.origin, speed > 200.0f ? 1.0f : 0.5f);
            p.footstepTimer = speed > 200.0f ? 0.32f : 0.45f;
        }
    } else {
        p.footstepTimer = 0.1f;
    }
}

void GameWorld::propImpactSounds() {
    if (propMotion_.size() < entities_.size()) propMotion_.resize(entities_.size());
    for (auto& e : entities_) {
        if (!e || !e->physicsDriven) continue;
        auto& m = propMotion_[static_cast<size_t>(e->index)];
        Vec3 v = physics_->linearVelocity(e->physicsBody);
        float change = glm::length(v - m.velocity);
        bool held = false;
        for (auto* p : players()) held |= p->heldProp == e->index;
        // A sudden change in velocity that isn't someone carrying it = an impact.
        if (change > 140.0f && !held && time_ - m.lastImpact > 0.15) {
            emitSound("Physics.WoodImpact", e.get(), e->origin, std::clamp(change / 500.0f, 0.2f, 1.0f));
            m.lastImpact = time_;
        }
        m.velocity = v;
    }
}

void GameWorld::pushTouchedProps(Player& p, const Vec3& playerVelocity) {
    if (moveTouches_.empty()) return;
    std::sort(moveTouches_.begin(), moveTouches_.end());
    moveTouches_.erase(std::unique(moveTouches_.begin(), moveTouches_.end()), moveTouches_.end());
    Vec3 flat(playerVelocity.x, playerVelocity.y, 0);
    float speed = glm::length(flat);
    if (speed < 10.0f) return;
    Vec3 dir = flat / speed;
    for (int idx : moveTouches_) {
        BaseEntity* e = entity(idx);
        if (!e || !e->physicsDriven || idx == p.heldProp) continue;
        // Nudge the prop toward walking speed, more gently for heavy ones.
        float mass = std::max(1.0f, physics_->mass(e->physicsBody));
        Vec3 v = physics_->linearVelocity(e->physicsBody);
        float along = glm::dot(v, dir);
        float want = std::min(speed, 200.0f) * std::clamp(40.0f / mass, 0.1f, 1.0f);
        if (along < want) {
            physics_->wake(e->physicsBody);
            physics_->setLinearVelocity(e->physicsBody, v + dir * (want - along) * 0.5f);
        }
    }
}

bool GameWorld::playerOverlaps(const Player& p, const BaseEntity& e, const Vec3& playerOrigin) const {
    if (!map_) return false;
    if (!boxesOverlap(playerOrigin + p.mins(), playerOrigin + p.maxs(), e.absMins(), e.absMaxs())) return false;
    if (e.brushModel <= 0) return true;
    auto tr = bsp::traceBoxModel(*map_, e.brushModel, e.origin, playerOrigin, playerOrigin, p.mins(), p.maxs(),
                                 bsp::CONTENTS_SOLID | bsp::CONTENTS_PLAYERCLIP);
    return tr.startSolid;
}

void GameWorld::touchTriggers(Player& p) {
    std::vector<int> now;
    Vec3 pmin = p.state.origin + p.mins(), pmax = p.state.origin + p.maxs();
    for (auto& e : entities_) {
        if (!e || e->solid != Solid::Trigger) continue;
        if (boxesOverlap(pmin, pmax, e->absMins(), e->absMaxs())) now.push_back(e->index);
    }
    for (int idx : now)
        if (std::find(p.touching.begin(), p.touching.end(), idx) == p.touching.end())
            if (auto* e = entity(idx)) e->startTouch(&p);
    for (int idx : p.touching)
        if (std::find(now.begin(), now.end(), idx) == now.end())
            if (auto* e = entity(idx)) e->endTouch(&p);
    p.touching = std::move(now);
}

bool GameWorld::tryPush(BaseEntity& mover, const Vec3& delta) {
    Vec3 oldOrigin = mover.origin;
    mover.origin += delta;
    if (mover.solid != Solid::Bsp) return true;

    std::vector<std::pair<Player*, Vec3>> moved; // to undo if blocked
    for (auto* p : players()) {
        if (p->state.moveType == MOVETYPE_NOCLIP || p->state.dead) continue;
        bool riding = p->state.groundEntity == mover.index && (p->state.flags & FL_ONGROUND);
        bool hit = playerOverlaps(*p, mover, p->state.origin);
        if (!riding && !hit) continue;

        // Move the player along with the pusher. Trace against everything except the mover.
        Solid saved = mover.solid;
        mover.solid = Solid::Not;
        Vec3 from = p->state.origin;
        moving_ = p;
        auto tr = traceBox(from, from + delta, p->mins(), p->maxs(), kPlayerMask);
        moving_ = nullptr;
        mover.solid = saved;
        if (!tr.startSolid && tr.fraction == 1.0f) {
            p->state.origin = tr.endPos;
            p->origin = tr.endPos;
            moved.emplace_back(p, from);
            if (!playerOverlaps(*p, mover, p->state.origin)) continue;
        }
        // Couldn't carry this player all the way. If the mover still doesn't intersect them
        // (e.g. a platform dropping away), leave them; otherwise the mover is blocked.
        p->state.origin = from;
        p->origin = from;
        if (!playerOverlaps(*p, mover, from)) continue;
        for (auto& [mp, origin] : moved) {
            mp->state.origin = origin;
            mp->origin = origin;
        }
        mover.origin = oldOrigin;
        mover.blocked(p);
        return false;
    }
    return true;
}

void GameWorld::deliverInput(const std::string& target, const std::string& input, const std::string& param,
                             BaseEntity* activator, BaseEntity* caller) {
    std::vector<BaseEntity*> targets;
    if (nameEquals(target, "!activator")) { if (activator) targets.push_back(activator); }
    else if (nameEquals(target, "!caller")) { if (caller) targets.push_back(caller); }
    else if (nameEquals(target, "!self")) { if (caller) targets.push_back(caller); }
    else if (nameEquals(target, "!player")) { if (auto* p = player(0)) targets.push_back(p); else if (!players().empty()) targets.push_back(players()[0]); }
    else targets = findByName(target);
    if (targets.empty()) {
        log::warn("Entity I/O: no entity named '{}' for input '{}'", target, input);
        return;
    }
    for (auto* t : targets) {
        if (developer_io.getBool())
            log::info("(t={:.2f}) {} '{}' -> {} '{}' : {}({})", time_, caller ? caller->classname : "?",
                      caller ? caller->targetname : "", t->classname, t->targetname, input, param);
        if (!t->acceptInput(input, activator, caller, param))
            log::warn("Entity I/O: {} '{}' has no input '{}'", t->classname, t->targetname, input);
    }
}

void GameWorld::clientCommand(int slot, const std::string& text) {
    Player* p = player(slot);
    if (!p) return;
    if (text == "noclip") {
        if (!console::cheatsAllowed()) {
            log::warn("Can't use noclip unless sv_cheats is 1");
            return;
        }
        auto& s = p->state;
        s.moveType = s.moveType == MOVETYPE_NOCLIP ? MOVETYPE_WALK : MOVETYPE_NOCLIP;
        s.velocity = Vec3(0);
        log::info("{}: noclip {}", p->name, s.moveType == MOVETYPE_NOCLIP ? "ON" : "OFF");
        return;
    }
    if (text.rfind("ent_fire ", 0) == 0) {
        if (!console::cheatsAllowed()) return;
        // ent_fire <target> <input> [param]
        auto first = text.find(' ', 9);
        std::string target = text.substr(9, first == std::string::npos ? std::string::npos : first - 9);
        std::string rest = first == std::string::npos ? "" : text.substr(first + 1);
        auto second = rest.find(' ');
        std::string input = rest.substr(0, second);
        std::string param = second == std::string::npos ? "" : rest.substr(second + 1);
        if (target.empty() || input.empty()) return;
        deliverInput(target, input, param, p, p);
        return;
    }
    if (text == "kill") {
        damagePlayer(*p, 1000, nullptr, "suicide");
        return;
    }
    if (text.rfind("setpos ", 0) == 0) {
        if (!console::cheatsAllowed()) return;
        setPlayerOrigin(slot, parseVec3(text.substr(7)));
        return;
    }
    log::debug("Unknown client command '{}'", text);
}

void GameWorld::frame(float dt, uint32_t tick) {
    time_ += dt;
    tick_ = tick;
    // Physics: kinematic bodies follow their entities, then simulate, then props follow bodies.
    if (physics_) {
        for (auto& e : entities_)
            if (e && !e->physicsDriven && e->physicsBody.valid()) physics_->moveKinematic(e->physicsBody, e->origin, dt);
        physics_->step(dt);
        for (auto& e : entities_)
            if (e && e->physicsDriven) physics_->getTransform(e->physicsBody, e->origin, e->angles);
        propImpactSounds();
    }
    // Think functions. Index loop: thinking can spawn or remove entities.
    for (size_t i = 0; i < entities_.size(); ++i) {
        BaseEntity* e = entities_[i].get();
        if (!e || e->nextThink_ <= 0 || e->nextThink_ > time_) continue;
        e->nextThink_ = 0;
        e->think();
    }
    events_.service(*this);
    for (auto& e : entities_) {
        if (!e || !e->markedForDeletion || dynamic_cast<Player*>(e.get())) continue;
        for (auto* p : players()) {
            std::erase(p->touching, e->index);
            if (p->heldProp == e->index) p->heldProp = -1;
        }
        if (e->physicsBody.valid() && physics_) physics_->removeBody(e->physicsBody);
        e.reset();
    }
    recordHistory(tick);
}

} // namespace k::game
