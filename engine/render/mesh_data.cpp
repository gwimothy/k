#include "render/mesh.h"

#include <cmath>
#include <limits>

namespace k {

Vec2 worldAlignedUV(const Vec3& p, const Vec3& n, float size) {
    Vec3 a = glm::abs(n);
    if (a.z >= a.x && a.z >= a.y) return Vec2(p.x, -p.y) / size;
    if (a.x >= a.y) return Vec2(p.y, -p.z) / size;
    return Vec2(p.x, -p.z) / size;
}

std::vector<uint32_t>& MeshData::sectionFor(const std::string& material) {
    for (auto& s : sections)
        if (s.material == material) return s.indices;
    sections.push_back({material, {}});
    return sections.back().indices;
}

void MeshData::addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const std::string& material,
                       float size) {
    Vec3 cr = glm::cross(b - a, c - a);
    if (glm::dot(cr, cr) < 1e-12f) cr = glm::cross(c - a, d - a); // first triangle degenerate
    Vec3 n = glm::normalize(cr);
    auto base = static_cast<uint32_t>(vertices.size());
    for (const Vec3* p : {&a, &b, &c, &d}) vertices.push_back({*p, n, worldAlignedUV(*p, n, size), Vec2(0)});
    sectionFor(material).insert(sectionFor(material).end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void MeshData::addBox(const Vec3& lo, const Vec3& hi, const std::string& mat) {
    Vec3 p000{lo.x, lo.y, lo.z}, p100{hi.x, lo.y, lo.z}, p010{lo.x, hi.y, lo.z}, p110{hi.x, hi.y, lo.z};
    Vec3 p001{lo.x, lo.y, hi.z}, p101{hi.x, lo.y, hi.z}, p011{lo.x, hi.y, hi.z}, p111{hi.x, hi.y, hi.z};
    addQuad(p001, p101, p111, p011, mat); // top (+Z)
    addQuad(p000, p010, p110, p100, mat); // bottom (-Z)
    addQuad(p100, p110, p111, p101, mat); // +X
    addQuad(p000, p001, p011, p010, mat); // -X
    addQuad(p010, p011, p111, p110, mat); // +Y
    addQuad(p000, p100, p101, p001, mat); // -Y
}

void MeshData::addRoom(const Vec3& lo, const Vec3& hi, const std::string& floorMat, const std::string& wallMat,
                       const std::string& ceilMat) {
    Vec3 p000{lo.x, lo.y, lo.z}, p100{hi.x, lo.y, lo.z}, p010{lo.x, hi.y, lo.z}, p110{hi.x, hi.y, lo.z};
    Vec3 p001{lo.x, lo.y, hi.z}, p101{hi.x, lo.y, hi.z}, p011{lo.x, hi.y, hi.z}, p111{hi.x, hi.y, hi.z};
    // Same faces as addBox with reversed winding so normals point inward.
    addQuad(p000, p100, p110, p010, floorMat);
    addQuad(p001, p011, p111, p101, ceilMat);
    addQuad(p100, p101, p111, p110, wallMat);
    addQuad(p000, p010, p011, p001, wallMat);
    addQuad(p010, p110, p111, p011, wallMat);
    addQuad(p000, p001, p101, p100, wallMat);
}

void MeshData::addRampX(const Vec3& lo, const Vec3& hi, const std::string& mat) {
    Vec3 a{lo.x, lo.y, lo.z}, b{hi.x, lo.y, lo.z}, c{hi.x, hi.y, lo.z}, d{lo.x, hi.y, lo.z};
    Vec3 b2{hi.x, lo.y, hi.z}, c2{hi.x, hi.y, hi.z};
    addQuad(a, b2, c2, d, mat); // slope
    addQuad(b, c, c2, b2, mat); // back (+X)
    addQuad(a, b, b2, b2, mat); // sides (triangles as degenerate quads)
    addQuad(d, c2, c, c, mat);
}

size_t MeshData::triangleCount() const {
    size_t n = 0;
    for (auto& s : sections) n += s.indices.size() / 3;
    return n;
}

void MeshData::bounds(Vec3& mins, Vec3& maxs) const {
    mins = Vec3(std::numeric_limits<float>::max());
    maxs = Vec3(std::numeric_limits<float>::lowest());
    for (auto& v : vertices) {
        mins = glm::min(mins, v.position);
        maxs = glm::max(maxs, v.position);
    }
    if (vertices.empty()) mins = maxs = Vec3(0);
}


} // namespace k
