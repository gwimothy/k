#include "physics.h"

#include "core/log.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>

namespace k::game {
namespace {

constexpr float kToMeters = 0.0254f;
constexpr float kToUnits = 1.0f / kToMeters;
constexpr float kDensity = 60.0f; // kg/m^3: props are mostly hollow (a 32-unit crate is ~32 kg)

namespace Layers {
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving = 1;
constexpr JPH::ObjectLayer Count = 2;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NonMoving(0);
constexpr JPH::BroadPhaseLayer Moving(1);
constexpr uint32_t Count = 2;
} // namespace BroadPhaseLayers

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    uint32_t GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
        return layer == Layers::Moving || bp == BroadPhaseLayers::Moving;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::Moving || b == Layers::Moving;
    }
};

JPH::Vec3 toJolt(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z) * kToMeters; }
Vec3 fromJolt(JPH::Vec3Arg v) { return Vec3(v.GetX(), v.GetY(), v.GetZ()) * kToUnits; }

// Matches the renderer: R = Rz(yaw) * Ry(pitch) * Rx(roll).
JPH::Quat anglesToQuat(const QAngle& a) {
    return JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), deg2rad(a.yaw)) *
           JPH::Quat::sRotation(JPH::Vec3::sAxisY(), deg2rad(a.pitch)) *
           JPH::Quat::sRotation(JPH::Vec3::sAxisX(), deg2rad(a.roll));
}

QAngle quatToAngles(JPH::QuatArg q) {
    JPH::Mat44 m = JPH::Mat44::sRotation(q);
    JPH::Vec3 c0 = m.GetColumn3(0), c1 = m.GetColumn3(1), c2 = m.GetColumn3(2);
    float pitch = std::asin(std::clamp(-c0.GetZ(), -1.0f, 1.0f));
    float yaw = std::atan2(c0.GetY(), c0.GetX());
    float roll = std::atan2(c1.GetZ(), c2.GetZ());
    return {rad2deg(pitch), rad2deg(yaw), rad2deg(roll)};
}

void initJoltOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

} // namespace

std::vector<Vec3> brushVertices(const bsp::BspData& map, const bsp::Brush& brush) {
    std::vector<bsp::Plane> planes;
    for (uint32_t i = 0; i < brush.numSides; ++i)
        planes.push_back(map.planes[static_cast<size_t>(map.brushSides[brush.firstSide + i].plane)]);
    std::vector<Vec3> out;
    for (size_t a = 0; a < planes.size(); ++a)
        for (size_t b = a + 1; b < planes.size(); ++b)
            for (size_t c = b + 1; c < planes.size(); ++c) {
                const Vec3 &n1 = planes[a].normal, &n2 = planes[b].normal, &n3 = planes[c].normal;
                float det = glm::dot(n1, glm::cross(n2, n3));
                if (std::fabs(det) < 1e-6f) continue;
                Vec3 p = (glm::cross(n2, n3) * planes[a].dist + glm::cross(n3, n1) * planes[b].dist +
                          glm::cross(n1, n2) * planes[c].dist) / det;
                bool inside = true;
                for (auto& pl : planes)
                    if (pl.distance(p) > 0.01f) { inside = false; break; }
                if (!inside) continue;
                bool dup = false;
                for (auto& q : out) dup |= glm::length(q - p) < 0.01f;
                if (!dup) out.push_back(p);
            }
    return out;
}

struct PhysicsWorld::Impl {
    BPLayerInterface bpLayers;
    ObjectVsBroadPhaseFilter objVsBp;
    ObjectPairFilter objPair;
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    JPH::PhysicsSystem system;
    std::vector<JPH::BodyID> bodies;

    JPH::BodyInterface& bi() { return system.GetBodyInterface(); }
    const JPH::BodyInterface& bi() const { return system.GetBodyInterface(); }

    PhysicsBody add(const JPH::BodyCreationSettings& settings) {
        JPH::Body* body = bi().CreateBody(settings);
        if (!body) {
            log::warn("Physics: out of bodies");
            return {};
        }
        bi().AddBody(body->GetID(), settings.mMotionType == JPH::EMotionType::Static ? JPH::EActivation::DontActivate
                                                                                    : JPH::EActivation::Activate);
        bodies.push_back(body->GetID());
        return {body->GetID().GetIndexAndSequenceNumber()};
    }
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {
    initJoltOnce();
    impl_->temp = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 4);
    impl_->jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
    impl_->system.Init(4096, 0, 4096, 4096, impl_->bpLayers, impl_->objVsBp, impl_->objPair);
    setGravity(600.0f); // HL2's physics gravity
}

PhysicsWorld::~PhysicsWorld() {
    for (auto id : impl_->bodies) {
        impl_->bi().RemoveBody(id);
        impl_->bi().DestroyBody(id);
    }
}

void PhysicsWorld::setGravity(float g) { impl_->system.SetGravity(JPH::Vec3(0, 0, -g * kToMeters)); }

size_t PhysicsWorld::bodyCount() const { return impl_->bodies.size(); }

static JPH::RefConst<JPH::Shape> hullShape(const std::vector<Vec3>& points, const Vec3& offset) {
    if (points.size() < 4) return nullptr;
    JPH::Array<JPH::Vec3> pts;
    for (auto& p : points) pts.push_back(toJolt(p - offset));
    JPH::ConvexHullShapeSettings settings(pts, 0.005f);
    auto result = settings.Create();
    if (result.HasError()) return nullptr;
    return result.Get();
}

void PhysicsWorld::loadWorld(const bsp::BspData& map) {
    JPH::StaticCompoundShapeSettings compound;
    int count = 0;
    // World brushes are the ones referenced by leaves (brush entities have their own bodies).
    std::vector<char> isWorld(map.brushes.size(), 0);
    for (uint32_t b : map.leafBrushes) isWorld[b] = 1;
    for (size_t i = 0; i < map.brushes.size(); ++i) {
        const auto& br = map.brushes[i];
        if (!isWorld[i] || !(br.contents & bsp::CONTENTS_SOLID)) continue;
        auto verts = brushVertices(map, br);
        auto shape = hullShape(verts, Vec3(0));
        if (!shape) continue;
        compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), shape);
        ++count;
    }
    if (count == 0) return;
    auto result = compound.Create();
    if (result.HasError()) {
        log::warn("Physics: couldn't build world collision: {}", result.GetError().c_str());
        return;
    }
    JPH::BodyCreationSettings settings(result.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Static, Layers::NonMoving);
    impl_->add(settings);
    log::debug("Physics: world collision from {} brushes", count);
}

PhysicsBody PhysicsWorld::addBrushModel(const bsp::BspData& map, int model, const Vec3& origin) {
    if (model <= 0 || static_cast<size_t>(model) >= map.models.size()) return {};
    const auto& m = map.models[static_cast<size_t>(model)];
    JPH::StaticCompoundShapeSettings compound;
    int count = 0;
    for (uint32_t i = 0; i < m.numBrushes; ++i) {
        const auto& br = map.brushes[m.firstBrush + i];
        if (!(br.contents & bsp::CONTENTS_SOLID)) continue;
        auto shape = hullShape(brushVertices(map, br), Vec3(0));
        if (!shape) continue;
        compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), shape);
        ++count;
    }
    if (count == 0) return {};
    auto result = compound.Create();
    if (result.HasError()) return {};
    JPH::BodyCreationSettings settings(result.Get(), JPH::RVec3(toJolt(origin)), JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Kinematic, Layers::Moving);
    return impl_->add(settings);
}

void PhysicsWorld::moveKinematic(PhysicsBody body, const Vec3& origin, float dt) {
    if (!body.valid()) return;
    impl_->bi().MoveKinematic(JPH::BodyID(body.id), JPH::RVec3(toJolt(origin)), JPH::Quat::sIdentity(), dt);
}

PhysicsBody PhysicsWorld::addDynamicHull(const std::vector<Vec3>& points, const Vec3& origin, const QAngle& angles,
                                         float massScale) {
    // Center the hull on its bounds so the body rotates around its middle.
    Vec3 lo(1e30f), hi(-1e30f);
    for (auto& p : points) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    auto shape = hullShape(points, Vec3(0));
    if (!shape) return addDynamicBox(lo, hi, origin, angles, massScale);
    JPH::BodyCreationSettings settings(shape, JPH::RVec3(toJolt(origin)), anglesToQuat(angles),
                                       JPH::EMotionType::Dynamic, Layers::Moving);
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = std::max(1.0f, shape->GetVolume() * kDensity * massScale);
    settings.mFriction = 0.6f;
    settings.mRestitution = 0.2f;
    settings.mLinearDamping = 0.05f;
    settings.mAngularDamping = 0.1f;
    return impl_->add(settings);
}

PhysicsBody PhysicsWorld::addDynamicBox(const Vec3& mins, const Vec3& maxs, const Vec3& origin, const QAngle& angles,
                                        float massScale) {
    Vec3 half = glm::max((maxs - mins) * 0.5f, Vec3(1));
    Vec3 center = (mins + maxs) * 0.5f;
    std::vector<Vec3> pts;
    for (int i = 0; i < 8; ++i)
        pts.push_back(center + Vec3((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z));
    auto shape = hullShape(pts, Vec3(0));
    if (!shape) return {};
    JPH::BodyCreationSettings settings(shape, JPH::RVec3(toJolt(origin)), anglesToQuat(angles),
                                       JPH::EMotionType::Dynamic, Layers::Moving);
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = std::max(1.0f, shape->GetVolume() * kDensity * massScale);
    settings.mFriction = 0.6f;
    settings.mRestitution = 0.2f;
    return impl_->add(settings);
}

void PhysicsWorld::removeBody(PhysicsBody body) {
    if (!body.valid()) return;
    JPH::BodyID id(body.id);
    impl_->bi().RemoveBody(id);
    impl_->bi().DestroyBody(id);
    std::erase(impl_->bodies, id);
}

void PhysicsWorld::step(float dt) {
    // Collision steps: more for big frame times keeps fast props from tunnelling.
    int steps = dt > 1.0f / 50.0f ? 2 : 1;
    impl_->system.Update(dt, steps, impl_->temp.get(), impl_->jobs.get());
}

void PhysicsWorld::getTransform(PhysicsBody body, Vec3& origin, QAngle& angles) const {
    if (!body.valid()) return;
    JPH::RVec3 pos;
    JPH::Quat rot;
    impl_->bi().GetPositionAndRotation(JPH::BodyID(body.id), pos, rot);
    origin = fromJolt(JPH::Vec3(pos));
    angles = quatToAngles(rot);
}

Vec3 PhysicsWorld::linearVelocity(PhysicsBody body) const {
    return body.valid() ? fromJolt(impl_->bi().GetLinearVelocity(JPH::BodyID(body.id))) : Vec3(0);
}

void PhysicsWorld::setLinearVelocity(PhysicsBody body, const Vec3& v) {
    if (body.valid()) impl_->bi().SetLinearVelocity(JPH::BodyID(body.id), toJolt(v));
}

void PhysicsWorld::setAngularVelocity(PhysicsBody body, const Vec3& w) {
    if (body.valid()) impl_->bi().SetAngularVelocity(JPH::BodyID(body.id), JPH::Vec3(w.x, w.y, w.z));
}

void PhysicsWorld::addImpulse(PhysicsBody body, const Vec3& impulse) {
    if (body.valid()) impl_->bi().AddImpulse(JPH::BodyID(body.id), toJolt(impulse));
}

float PhysicsWorld::mass(PhysicsBody body) const {
    if (!body.valid()) return 0;
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), JPH::BodyID(body.id));
    if (!lock.Succeeded()) return 0;
    float inv = lock.GetBody().GetMotionProperties() ? lock.GetBody().GetMotionProperties()->GetInverseMass() : 0;
    return inv > 0 ? 1.0f / inv : 0;
}

void PhysicsWorld::bounds(PhysicsBody body, Vec3& mins, Vec3& maxs) const {
    if (!body.valid()) return;
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), JPH::BodyID(body.id));
    if (!lock.Succeeded()) return;
    JPH::AABox box = lock.GetBody().GetWorldSpaceBounds();
    mins = fromJolt(box.mMin);
    maxs = fromJolt(box.mMax);
}

bool PhysicsWorld::isSleeping(PhysicsBody body) const {
    return body.valid() && !impl_->bi().IsActive(JPH::BodyID(body.id));
}

void PhysicsWorld::wake(PhysicsBody body) {
    if (body.valid()) impl_->bi().ActivateBody(JPH::BodyID(body.id));
}

} // namespace k::game
