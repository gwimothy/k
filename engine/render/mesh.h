#pragma once

#include "shared/kmath.h"

#include <cstdint>
#include <vector>

namespace k {

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec3 color;
};

// CPU-side geometry builder.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // A quad from four corners in counter-clockwise order (seen from the front).
    void addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& color);
    // A solid box with outward-facing faces.
    void addBox(const Vec3& mins, const Vec3& maxs, const Vec3& color);
    // A hollow room: the inside faces of a box.
    void addRoom(const Vec3& mins, const Vec3& maxs, const Vec3& floorColor, const Vec3& wallColor,
                 const Vec3& ceilingColor);
    // A ramp rising along +X from mins.x to maxs.x.
    void addRampX(const Vec3& mins, const Vec3& maxs, const Vec3& color);
};

// GPU mesh (VAO + buffers).
class Mesh {
public:
    Mesh() = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    ~Mesh();

    void upload(const MeshData& data);
    void draw() const;
    bool valid() const { return vao_ != 0; }
    size_t triangleCount() const { return indexCount_ / 3; }

private:
    uint32_t vao_ = 0, vbo_ = 0, ebo_ = 0;
    int32_t indexCount_ = 0;
};

} // namespace k
