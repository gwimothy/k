#include "render/mesh.h"

#include <glad/gl.h>

#include <cstddef>

namespace k {

void MeshData::addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& color) {
    Vec3 n = glm::normalize(glm::cross(b - a, c - a));
    auto base = static_cast<uint32_t>(vertices.size());
    for (const Vec3* p : {&a, &b, &c, &d}) vertices.push_back({*p, n, color});
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void MeshData::addBox(const Vec3& lo, const Vec3& hi, const Vec3& color) {
    // Corners
    Vec3 p000{lo.x, lo.y, lo.z}, p100{hi.x, lo.y, lo.z}, p010{lo.x, hi.y, lo.z}, p110{hi.x, hi.y, lo.z};
    Vec3 p001{lo.x, lo.y, hi.z}, p101{hi.x, lo.y, hi.z}, p011{lo.x, hi.y, hi.z}, p111{hi.x, hi.y, hi.z};
    addQuad(p001, p101, p111, p011, color); // top (+Z)
    addQuad(p000, p010, p110, p100, color); // bottom (-Z)
    addQuad(p100, p110, p111, p101, color); // +X
    addQuad(p000, p001, p011, p010, color); // -X
    addQuad(p010, p011, p111, p110, color); // +Y
    addQuad(p000, p100, p101, p001, color); // -Y
}

void MeshData::addRoom(const Vec3& lo, const Vec3& hi, const Vec3& floorC, const Vec3& wallC, const Vec3& ceilC) {
    Vec3 p000{lo.x, lo.y, lo.z}, p100{hi.x, lo.y, lo.z}, p010{lo.x, hi.y, lo.z}, p110{hi.x, hi.y, lo.z};
    Vec3 p001{lo.x, lo.y, hi.z}, p101{hi.x, lo.y, hi.z}, p011{lo.x, hi.y, hi.z}, p111{hi.x, hi.y, hi.z};
    // Same faces as addBox but with reversed winding so normals point inward.
    addQuad(p000, p100, p110, p010, floorC); // floor faces +Z
    addQuad(p001, p011, p111, p101, ceilC);  // ceiling faces -Z
    addQuad(p100, p101, p111, p110, wallC);  // +X wall faces -X
    addQuad(p000, p010, p011, p001, wallC);  // -X wall faces +X
    addQuad(p010, p110, p111, p011, wallC);  // +Y wall faces -Y
    addQuad(p000, p001, p101, p100, wallC);  // -Y wall faces +Y
}

void MeshData::addRampX(const Vec3& lo, const Vec3& hi, const Vec3& color) {
    Vec3 a{lo.x, lo.y, lo.z}, b{hi.x, lo.y, lo.z}, c{hi.x, hi.y, lo.z}, d{lo.x, hi.y, lo.z};
    Vec3 b2{hi.x, lo.y, hi.z}, c2{hi.x, hi.y, hi.z};
    addQuad(a, b2, c2, d, color); // slope
    addQuad(b, c, c2, b2, color); // back (+X)
    // Side triangles as degenerate quads.
    addQuad(a, b, b2, b2, color);
    addQuad(d, c2, c, c, color);
}

Mesh::~Mesh() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &vbo_);
        glDeleteBuffers(1, &ebo_);
    }
}

void Mesh::upload(const MeshData& data) {
    if (!vao_) {
        glCreateVertexArrays(1, &vao_);
        glCreateBuffers(1, &vbo_);
        glCreateBuffers(1, &ebo_);
    }
    glNamedBufferData(vbo_, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)), data.vertices.data(),
                      GL_STATIC_DRAW);
    glNamedBufferData(ebo_, static_cast<GLsizeiptr>(data.indices.size() * sizeof(uint32_t)), data.indices.data(),
                      GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao_, ebo_);
    const GLuint offsets[] = {offsetof(Vertex, position), offsetof(Vertex, normal), offsetof(Vertex, color)};
    for (GLuint i = 0; i < 3; ++i) {
        glEnableVertexArrayAttrib(vao_, i);
        glVertexArrayAttribFormat(vao_, i, 3, GL_FLOAT, GL_FALSE, offsets[i]);
        glVertexArrayAttribBinding(vao_, i, 0);
    }
    indexCount_ = static_cast<int32_t>(data.indices.size());
}

void Mesh::draw() const {
    if (!vao_ || indexCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

} // namespace k
