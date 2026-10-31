#pragma once

#include "shared/kmath.h"

#include <cstdint>
#include <string>
#include <vector>

namespace k {

struct Material;
class MaterialSystem;

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec2 lightmapUV; // filled by the map compiler in Phase 3
};

// CPU-side geometry: shared vertices plus one index list per material.
struct MeshData {
    struct Section {
        std::string material;
        std::vector<uint32_t> indices;
    };
    std::vector<Vertex> vertices;
    std::vector<Section> sections;

    std::vector<uint32_t>& sectionFor(const std::string& material);

    // A quad from four corners in counter-clockwise order (seen from the front), with
    // world-aligned texture coordinates (`texelsPerUnit` texture repeats every 1/texelsPerUnit units).
    void addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const std::string& material,
                 float textureWorldSize = 256.0f);
    // A solid box with outward-facing faces.
    void addBox(const Vec3& mins, const Vec3& maxs, const std::string& material);
    // A hollow room: the inside faces of a box.
    void addRoom(const Vec3& mins, const Vec3& maxs, const std::string& floorMat, const std::string& wallMat,
                 const std::string& ceilingMat);
    // A ramp rising along +X from mins.x to maxs.x.
    void addRampX(const Vec3& mins, const Vec3& maxs, const std::string& material);

    size_t triangleCount() const;
    void bounds(Vec3& mins, Vec3& maxs) const;
};

// Planar texture coordinates for a point on a face with the given normal (Source-style
// world alignment: the texture is projected along the face's dominant axis).
Vec2 worldAlignedUV(const Vec3& p, const Vec3& normal, float textureWorldSize);

// GPU mesh: one VAO with a draw range per material.
class Mesh {
public:
    struct DrawRange {
        Material* material;
        int32_t firstIndex;
        int32_t indexCount;
    };

    Mesh() = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    ~Mesh();

    void upload(const MeshData& data, MaterialSystem& materials);
    void bindVertexArray() const;
    void drawRange(const DrawRange& r) const;
    const std::vector<DrawRange>& ranges() const { return ranges_; }
    bool valid() const { return vao_ != 0; }
    size_t triangleCount() const { return static_cast<size_t>(indexCount_) / 3; }
    const Vec3& mins() const { return mins_; }
    const Vec3& maxs() const { return maxs_; }

private:
    uint32_t vao_ = 0, vbo_ = 0, ebo_ = 0;
    int32_t indexCount_ = 0;
    std::vector<DrawRange> ranges_;
    Vec3 mins_{0}, maxs_{0};
};

} // namespace k
