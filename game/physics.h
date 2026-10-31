#pragma once
// Rigid-body physics (Jolt). Server-side only: props are simulated here and networked.
//
// Engine units are inches; Jolt works in meters, so everything is converted at this
// boundary (1 unit = 0.0254 m). The world is one static compound of convex brush hulls;
// brush entities (doors, lifts) are kinematic bodies; prop_physics are dynamic bodies.

#include "shared/bspfile.h"
#include "shared/kmath.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace k::game {

struct PhysicsBody {
    uint32_t id = 0xFFFFFFFF; // Jolt BodyID (opaque)
    bool valid() const { return id != 0xFFFFFFFF; }
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Static collision from the map's world brushes (solid, not clip/sky-only).
    void loadWorld(const bsp::BspData& map);

    // A kinematic body for a brush entity model, positioned by its origin offset.
    PhysicsBody addBrushModel(const bsp::BspData& map, int model, const Vec3& origin);
    void moveKinematic(PhysicsBody body, const Vec3& origin, float dt);

    // A dynamic convex body from points (model vertices, local space), or a box.
    PhysicsBody addDynamicHull(const std::vector<Vec3>& localPoints, const Vec3& origin, const QAngle& angles,
                               float massScale);
    PhysicsBody addDynamicBox(const Vec3& mins, const Vec3& maxs, const Vec3& origin, const QAngle& angles,
                              float massScale);
    void removeBody(PhysicsBody body);

    void step(float dt);

    // Body state in engine units/conventions.
    void getTransform(PhysicsBody body, Vec3& origin, QAngle& angles) const;
    Vec3 linearVelocity(PhysicsBody body) const;
    void setLinearVelocity(PhysicsBody body, const Vec3& v);
    void setAngularVelocity(PhysicsBody body, const Vec3& radPerSec);
    void addImpulse(PhysicsBody body, const Vec3& impulseUnits); // mass * units/s
    float mass(PhysicsBody body) const;
    // World-space bounds of the body's shape.
    void bounds(PhysicsBody body, Vec3& mins, Vec3& maxs) const;
    bool isSleeping(PhysicsBody body) const;
    void wake(PhysicsBody body);

    void setGravity(float unitsPerSec2);
    size_t bodyCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Vertices of a convex brush (intersections of its planes). Exposed for tests.
std::vector<Vec3> brushVertices(const bsp::BspData& map, const bsp::Brush& brush);

} // namespace k::game
