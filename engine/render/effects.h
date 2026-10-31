#pragma once
// Client-side visual effects: particles (camera-facing quads) and impact decals.
// Spawned from server temp entities; purely cosmetic and never networked back.

#include "shared/kmath.h"

#include <cstdint>
#include <random>
#include <vector>

namespace k {

class ShaderLibrary;

class Effects {
public:
    Effects() = default;
    Effects(const Effects&) = delete;
    Effects& operator=(const Effects&) = delete;
    ~Effects();

    // type: proto::TempEntityType
    void spawn(uint8_t type, const Vec3& position, const Vec3& normal);
    void update(float dt);
    void draw(const Mat4& viewProj, const Vec3& cameraRight, const Vec3& cameraUp, ShaderLibrary& shaders);
    void clear();

    size_t particleCount() const { return particles_.size(); }
    size_t decalCount() const { return decals_.size(); }

private:
    struct Particle {
        Vec3 position, velocity;
        Vec4 color;
        float size, life, maxLife;
        float gravity;
        bool additive;
    };
    struct Decal {
        Vec3 position, normal, tangent, bitangent;
        float size;
        float seed;
    };
    struct Vertex {
        Vec3 position;
        Vec2 uv;
        Vec4 color;
    };
    void ensureBuffers();
    void emit(const Vec3& pos, const Vec3& dir, int count, const Vec4& color, float speed, float size, float life,
              float gravity, bool additive);

    std::vector<Particle> particles_;
    std::vector<Decal> decals_;
    size_t nextDecal_ = 0;
    std::mt19937 rng_{99};
    uint32_t vao_ = 0, vbo_ = 0;
};

} // namespace k
