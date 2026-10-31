#include "game_world.h"

#include "core/cvar.h"
#include "core/log.h"
#include "shared/bsp_trace.h"

#include <algorithm>
#include <cmath>

namespace k::game {
namespace {

// Movement settings (replicated so client prediction will match later).
ConVar sv_maxspeed("sv_maxspeed", "320", FCVAR_REPLICATED | FCVAR_NOTIFY, "Maximum player speed.");
ConVar sv_accelerate("sv_accelerate", "10", FCVAR_REPLICATED | FCVAR_NOTIFY, "Ground acceleration.");
ConVar sv_airaccelerate("sv_airaccelerate", "10", FCVAR_REPLICATED | FCVAR_NOTIFY, "Air acceleration.");
ConVar sv_friction("sv_friction", "4", FCVAR_REPLICATED | FCVAR_NOTIFY, "World friction.");
ConVar sv_stopspeed("sv_stopspeed", "100", FCVAR_REPLICATED | FCVAR_NOTIFY, "Minimum stopping speed on ground.");
ConVar sv_gravity("sv_gravity", "800", FCVAR_REPLICATED | FCVAR_NOTIFY, "World gravity.");
ConVar sv_stepsize("sv_stepsize", "18", FCVAR_REPLICATED | FCVAR_NOTIFY, "Maximum step-up height.");
ConVar sv_autobunnyhopping("sv_autobunnyhopping", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "Holding jump re-jumps on landing.");
ConVar sv_noclipspeed("sv_noclipspeed", "5", FCVAR_ARCHIVE | FCVAR_NOTIFY, "Noclip speed multiplier.");
ConVar sv_noclipaccelerate("sv_noclipaccelerate", "5", FCVAR_ARCHIVE | FCVAR_NOTIFY, "Noclip acceleration.");
ConVar developer_io("developer_io", "0", FCVAR_NONE, "Log every entity I/O event as it fires.");

constexpr float kUseDistance = 72.0f;

bool boxesOverlap(const Vec3& amin, const Vec3& amax, const Vec3& bmin, const Vec3& bmax) {
    return amin.x < bmax.x && amax.x > bmin.x && amin.y < bmax.y && amax.y > bmin.y && amin.z < bmax.z && amax.z > bmin.z;
}

// Ray vs AABB (slab test). Returns the entry distance or -1.
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
GameWorld::~GameWorld() = default;

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

    // Slot 0 is the world; slot 1 is reserved for the player.
    auto worldEnt = std::make_unique<BaseEntity>();
    worldEnt->classname = "worldspawn";
    worldEnt->world_ = this;
    entities_.push_back(std::move(worldEnt));
    auto p = std::make_unique<Player>();
    p->classname = "player";
    player_ = p.get();
    addEntity(std::move(p));

    if (map_) {
        for (size_t i = 1; i < map_->entities.size(); ++i) spawnEntity(map_->entities[i]);
        if (auto* start = map_->findEntity("info_player_start")) {
            Vec3 a = bsp::entityVec3(*start, "angles");
            spawnAngles_ = {a.x, a.y, a.z};
            player_->state.origin = bsp::entityVec3(*start, "origin");
        } else {
            log::warn("Map has no info_player_start; spawning at the origin");
        }
    }
    player_->state.viewAngles = spawnAngles_;
    player_->origin = player_->state.origin;

    for (auto& e : entities_)
        if (e) e->activate();
    buildModelTable();
    return true;
}

void GameWorld::levelShutdown() {
    events_.clear();
    touching_.clear();
    entities_.clear(); // before physics_: entities may own bodies
    physics_.reset();
    heldProp_ = -1;
    modelTable_.clear();
    player_ = nullptr;
    map_.reset();
}

void GameWorld::buildModelTable() {
    modelTable_.assign(1, ""); // 0 = no model
    for (auto& e : entities_) {
        if (!e || !e->networked) continue;
        std::string name = e->brushModel > 0 ? "*" + std::to_string(e->brushModel) : e->modelPath;
        if (!name.empty() && std::find(modelTable_.begin(), modelTable_.end(), name) == modelTable_.end())
            modelTable_.push_back(name);
    }
}

void GameWorld::buildEntityStates(std::vector<proto::EntityState>& out) const {
    out.clear();
    for (auto& e : entities_) {
        if (!e || !e->networked) continue;
        std::string name = e->brushModel > 0 ? "*" + std::to_string(e->brushModel) : e->modelPath;
        auto it = std::find(modelTable_.begin(), modelTable_.end(), name);
        proto::EntityState s;
        s.index = static_cast<uint16_t>(e->index);
        s.model = static_cast<uint16_t>(it == modelTable_.end() ? 0 : it - modelTable_.begin());
        s.origin = e->origin;
        s.angles = e->angles;
        s.effects = e->effects;
        out.push_back(s);
    }
}

bool GameWorld::takeAngleOverride(QAngle& angles) {
    if (!player_ || !player_->teleported) return false;
    player_->teleported = false;
    angles = player_->state.viewAngles;
    return true;
}

void GameWorld::clientPutInServer(const std::string& name) {
    if (player_) player_->name = name;
}

const proto::PlayerState& GameWorld::playerState() const {
    static const proto::PlayerState empty;
    return player_ ? player_->state : empty;
}

void GameWorld::setPlayerOrigin(const Vec3& origin) {
    if (!player_) return;
    player_->state.origin = origin;
    player_->state.velocity = Vec3(0);
    player_->origin = origin;
}

bsp::TraceResult GameWorld::traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                                     uint32_t mask) const {
    bsp::TraceResult best;
    best.endPos = end;
    if (map_) best = bsp::traceBox(*map_, start, end, mins, maxs, mask);
    if (best.allSolid || !map_) return best;
    // Physics props block like boxes (their world bounds). Overlaps at the start are ignored
    // so a prop that lands on the player can't trap them.
    for (auto& e : entities_) {
        if (!e || !e->physicsDriven || e->index == heldProp_) continue;
        Vec3 bmin = e->absMins() - maxs, bmax = e->absMaxs() - mins; // Minkowski-expanded
        Vec3 dir = end - start;
        float tEnter = 0, tExit = 1;
        int axis = -1;
        bool inside = true, miss = false;
        for (int i = 0; i < 3 && !miss; ++i) {
            if (start[i] < bmin[i] || start[i] > bmax[i]) inside = false;
            if (std::fabs(dir[i]) < 1e-7f) {
                if (start[i] < bmin[i] || start[i] > bmax[i]) miss = true;
                continue;
            }
            float t1 = (bmin[i] - start[i]) / dir[i], t2 = (bmax[i] - start[i]) / dir[i];
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tEnter) { tEnter = t1; axis = i; }
            tExit = std::min(tExit, t2);
            if (tEnter > tExit) miss = true;
        }
        if (miss || inside || axis < 0) continue;
        float len = glm::length(dir);
        float frac = std::max(0.0f, tEnter - (len > 0 ? bsp::kDistEpsilon / len : 0.0f));
        if (frac < best.fraction) {
            best.fraction = frac;
            best.endPos = start + dir * frac;
            best.normal = Vec3(0);
            best.normal[axis] = dir[axis] > 0 ? -1.0f : 1.0f;
            best.entity = e->index;
            best.contents = bsp::CONTENTS_SOLID;
            moveTouches_.push_back(e->index);
        }
    }
    for (auto& e : entities_) {
        if (!e || e->solid != Solid::Bsp || e->brushModel <= 0) continue;
        // Cheap reject: swept box vs the entity's bounds.
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

void GameWorld::runCommand(const UserCmd& cmd, float dt) {
    if (!player_) return;
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
    Vec3 velocityBefore = player_->state.velocity;
    proto::PlayerState stateBefore = player_->state;
    playerMove(player_->state, cmd, dt, params, map_ ? static_cast<const TraceWorld*>(this) : nullptr);
    player_->origin = player_->state.origin;
    player_->angles = player_->state.viewAngles;
    pushTouchedProps(velocityBefore);
    playerSounds(stateBefore, dt);

    if ((cmd.buttons & IN_USE) && !(player_->lastButtons & IN_USE)) handleUse();
    updateHeldProp(cmd, dt);
    player_->lastButtons = cmd.buttons;
    touchTriggers();
}

void GameWorld::handleUse() {
    Vec3 eye = player_->state.origin + Vec3(0, 0, player_->state.viewHeight);
    Vec3 forward;
    angleVectors(player_->state.viewAngles, &forward);
    // Nearest usable brush entity along the view ray, not behind a wall.
    float wallDist = kUseDistance;
    if (map_) wallDist = bsp::traceLine(*map_, eye, eye + forward * kUseDistance).fraction * kUseDistance;
    BaseEntity* best = nullptr;
    float bestDist = std::min(wallDist + 2.0f, kUseDistance);
    for (auto& e : entities_) {
        if (!e || e->brushModel <= 0 || e.get() == player_) continue;
        float d = rayBox(eye, forward, e->absMins() - Vec3(2), e->absMaxs() + Vec3(2));
        if (d >= 0 && d <= bestDist) {
            bestDist = d;
            best = e.get();
        }
    }
    if (best) {
        best->use(player_);
        return;
    }
    if (heldProp_ >= 0) { // drop
        heldProp_ = -1;
        return;
    }
    // Pick up the nearest light prop in front of us.
    float propDist = std::min(wallDist + 8.0f, 96.0f);
    for (auto& e : entities_) {
        if (!e || !e->physicsDriven) continue;
        float d = rayBox(eye, forward, e->absMins(), e->absMaxs());
        if (d >= 0 && d <= propDist && physics_->mass(e->physicsBody) <= 60.0f) {
            propDist = d;
            heldProp_ = e->index;
        }
    }
}

void GameWorld::updateHeldProp(const UserCmd& cmd, float dt) {
    BaseEntity* prop = entity(heldProp_);
    if (!prop || !prop->physicsDriven) {
        heldProp_ = -1;
        return;
    }
    Vec3 eye = player_->state.origin + Vec3(0, 0, player_->state.viewHeight);
    Vec3 forward;
    angleVectors(player_->state.viewAngles, &forward);
    Vec3 center = (prop->absMins() + prop->absMaxs()) * 0.5f;
    float radius = glm::length(prop->absMaxs() - prop->absMins()) * 0.5f;
    if (cmd.buttons & IN_ATTACK) {
        // Throw.
        physics_->setLinearVelocity(prop->physicsBody, forward * 700.0f + player_->state.velocity);
        heldProp_ = -1;
        return;
    }
    Vec3 target = eye + forward * (24.0f + radius);
    Vec3 toTarget = target - center;
    if (glm::length(toTarget) > 160.0f) { // snagged on something: let go
        heldProp_ = -1;
        return;
    }
    Vec3 vel = toTarget / std::max(dt, 1e-3f) * 0.4f + player_->state.velocity;
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

void GameWorld::playerSounds(const proto::PlayerState& before, float dt) {
    const auto& s = player_->state;
    if (s.moveType == MOVETYPE_NOCLIP) return;
    bool wasGround = before.flags & FL_ONGROUND, onGround = s.flags & FL_ONGROUND;
    if (wasGround && !onGround && s.velocity.z > 200.0f) emitSound("Player.Jump", nullptr, s.origin, 1.0f);
    if (!wasGround && onGround && before.velocity.z < -300.0f)
        emitSound("Player.Land", nullptr, s.origin, std::clamp(-before.velocity.z / 600.0f, 0.3f, 1.0f));
    // Footsteps: Source steps every ~0.3 s at run speed; quieter and slower when walking.
    float speed = glm::length(Vec2(s.velocity.x, s.velocity.y));
    if (onGround && speed > 90.0f && !(s.flags & FL_DUCKING)) {
        footstepTimer_ -= dt;
        if (footstepTimer_ <= 0) {
            emitSound("Player.Footstep", nullptr, s.origin, speed > 200.0f ? 1.0f : 0.5f);
            footstepTimer_ = speed > 200.0f ? 0.32f : 0.45f;
        }
    } else {
        footstepTimer_ = 0.1f;
    }
}

void GameWorld::propImpactSounds() {
    if (propMotion_.size() < entities_.size()) propMotion_.resize(entities_.size());
    for (auto& e : entities_) {
        if (!e || !e->physicsDriven) continue;
        auto& m = propMotion_[static_cast<size_t>(e->index)];
        Vec3 v = physics_->linearVelocity(e->physicsBody);
        float change = glm::length(v - m.velocity);
        // A sudden change in velocity that isn't the player carrying it = an impact.
        if (change > 140.0f && e->index != heldProp_ && time_ - m.lastImpact > 0.15) {
            emitSound("Physics.WoodImpact", e.get(), e->origin, std::clamp(change / 500.0f, 0.2f, 1.0f));
            m.lastImpact = time_;
        }
        m.velocity = v;
    }
}

void GameWorld::pushTouchedProps(const Vec3& playerVelocity) {
    if (moveTouches_.empty()) return;
    std::sort(moveTouches_.begin(), moveTouches_.end());
    moveTouches_.erase(std::unique(moveTouches_.begin(), moveTouches_.end()), moveTouches_.end());
    Vec3 flat(playerVelocity.x, playerVelocity.y, 0);
    float speed = glm::length(flat);
    if (speed < 10.0f) return;
    Vec3 dir = flat / speed;
    for (int idx : moveTouches_) {
        BaseEntity* e = entity(idx);
        if (!e || !e->physicsDriven || idx == heldProp_) continue;
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

bool GameWorld::playerOverlaps(const BaseEntity& e, const Vec3& playerOrigin) const {
    if (!player_ || !map_) return false;
    if (!boxesOverlap(playerOrigin + player_->mins(), playerOrigin + player_->maxs(), e.absMins(), e.absMaxs())) return false;
    if (e.brushModel <= 0) return true;
    auto tr = bsp::traceBoxModel(*map_, e.brushModel, e.origin, playerOrigin, playerOrigin, player_->mins(),
                                 player_->maxs(), bsp::CONTENTS_SOLID | bsp::CONTENTS_PLAYERCLIP);
    return tr.startSolid;
}

void GameWorld::touchTriggers() {
    std::vector<int> now;
    Vec3 pmin = player_->state.origin + player_->mins(), pmax = player_->state.origin + player_->maxs();
    for (auto& e : entities_) {
        if (!e || e->solid != Solid::Trigger) continue;
        if (boxesOverlap(pmin, pmax, e->absMins(), e->absMaxs())) now.push_back(e->index);
    }
    for (int idx : now)
        if (std::find(touching_.begin(), touching_.end(), idx) == touching_.end())
            if (auto* e = entity(idx)) e->startTouch(player_);
    for (int idx : touching_)
        if (std::find(now.begin(), now.end(), idx) == now.end())
            if (auto* e = entity(idx)) e->endTouch(player_);
    touching_ = std::move(now);
}

bool GameWorld::tryPush(BaseEntity& mover, const Vec3& delta) {
    Vec3 oldOrigin = mover.origin;
    mover.origin += delta;
    if (!player_ || player_->state.moveType == MOVETYPE_NOCLIP || mover.solid != Solid::Bsp) return true;

    bool riding = player_->state.groundEntity == mover.index && (player_->state.flags & FL_ONGROUND);
    bool hit = playerOverlaps(mover, player_->state.origin);
    if (!riding && !hit) return true;

    // Move the player along with the pusher. Trace against everything except the mover.
    Solid saved = mover.solid;
    mover.solid = Solid::Not;
    Vec3 from = player_->state.origin;
    auto tr = traceBox(from, from + delta, player_->mins(), player_->maxs(), kPlayerMask);
    mover.solid = saved;
    if (!tr.startSolid && tr.fraction == 1.0f) {
        player_->state.origin = tr.endPos;
        player_->origin = tr.endPos;
        if (!playerOverlaps(mover, player_->state.origin)) return true;
    }
    // The player couldn't be carried all the way. Leave them where they were; if the mover
    // still doesn't intersect them (e.g. a platform dropping away), let it move anyway.
    player_->state.origin = from;
    player_->origin = from;
    if (!playerOverlaps(mover, from)) return true;
    mover.origin = oldOrigin;
    mover.blocked(player_);
    return false;
}

void GameWorld::deliverInput(const std::string& target, const std::string& input, const std::string& param,
                             BaseEntity* activator, BaseEntity* caller) {
    std::vector<BaseEntity*> targets;
    if (nameEquals(target, "!activator")) { if (activator) targets.push_back(activator); }
    else if (nameEquals(target, "!caller")) { if (caller) targets.push_back(caller); }
    else if (nameEquals(target, "!self")) { if (caller) targets.push_back(caller); }
    else if (nameEquals(target, "!player")) { if (player_) targets.push_back(player_); }
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

void GameWorld::clientCommand(const std::string& text) {
    if (text == "noclip") {
        if (!console::cheatsAllowed()) {
            log::warn("Can't use noclip unless sv_cheats is 1");
            return;
        }
        auto& s = player_->state;
        s.moveType = s.moveType == MOVETYPE_NOCLIP ? MOVETYPE_WALK : MOVETYPE_NOCLIP;
        s.velocity = Vec3(0);
        log::info("noclip {}", s.moveType == MOVETYPE_NOCLIP ? "ON" : "OFF");
        return;
    }
    if (text.rfind("ent_fire ", 0) == 0) {
        // ent_fire <target> <input> [param]
        auto first = text.find(' ', 9);
        std::string target = text.substr(9, first == std::string::npos ? std::string::npos : first - 9);
        std::string rest = first == std::string::npos ? "" : text.substr(first + 1);
        auto second = rest.find(' ');
        std::string input = rest.substr(0, second);
        std::string param = second == std::string::npos ? "" : rest.substr(second + 1);
        if (target.empty() || input.empty()) {
            log::info("Usage: ent_fire <target> <input> [param]");
            return;
        }
        deliverInput(target, input, param, player_, player_);
        return;
    }
    log::debug("Unknown client command '{}'", text);
}

void GameWorld::frame(float dt) {
    time_ += dt;
    // Physics: kinematic bodies follow their entities, then simulate, then props follow bodies.
    if (physics_) {
        for (auto& e : entities_)
            if (e && !e->physicsDriven && e->physicsBody.valid()) physics_->moveKinematic(e->physicsBody, e->origin, dt);
        physics_->step(dt);
        for (auto& e : entities_)
            if (e && e->physicsDriven) physics_->getTransform(e->physicsBody, e->origin, e->angles);
        propImpactSounds();
    }
    // Think functions. Copy the list: thinking can spawn or remove entities.
    for (size_t i = 0; i < entities_.size(); ++i) {
        BaseEntity* e = entities_[i].get();
        if (!e || e->nextThink_ <= 0 || e->nextThink_ > time_) continue;
        e->nextThink_ = 0;
        e->think();
    }
    events_.service(*this);
    for (auto& e : entities_)
        if (e && e->markedForDeletion && e.get() != player_) {
            std::erase(touching_, e->index);
            if (e->physicsBody.valid() && physics_) physics_->removeBody(e->physicsBody);
            if (heldProp_ == e->index) heldProp_ = -1;
            e.reset();
        }
}

} // namespace k::game
