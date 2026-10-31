#include "render/mesh.h"

#include "render/material.h"

#include <glad/gl.h>

#include <cstddef>

namespace k {

Mesh::~Mesh() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &vbo_);
        glDeleteBuffers(1, &ebo_);
    }
}

void Mesh::upload(const MeshData& data, MaterialSystem& materials) {
    if (!vao_) {
        glCreateVertexArrays(1, &vao_);
        glCreateBuffers(1, &vbo_);
        glCreateBuffers(1, &ebo_);
    }
    std::vector<uint32_t> indices;
    ranges_.clear();
    for (auto& s : data.sections) {
        if (s.indices.empty()) continue;
        ranges_.push_back({materials.find(s.material), static_cast<int32_t>(indices.size()),
                           static_cast<int32_t>(s.indices.size())});
        indices.insert(indices.end(), s.indices.begin(), s.indices.end());
    }
    glNamedBufferData(vbo_, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)), data.vertices.data(),
                      GL_STATIC_DRAW);
    glNamedBufferData(ebo_, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao_, ebo_);
    struct Attr { GLint size; GLuint offset; };
    const Attr attrs[] = {{3, offsetof(Vertex, position)}, {3, offsetof(Vertex, normal)},
                          {2, offsetof(Vertex, uv)}, {2, offsetof(Vertex, lightmapUV)}};
    for (GLuint i = 0; i < 4; ++i) {
        glEnableVertexArrayAttrib(vao_, i);
        glVertexArrayAttribFormat(vao_, i, attrs[i].size, GL_FLOAT, GL_FALSE, attrs[i].offset);
        glVertexArrayAttribBinding(vao_, i, 0);
    }
    indexCount_ = static_cast<int32_t>(indices.size());
    data.bounds(mins_, maxs_);
}

void Mesh::bindVertexArray() const { glBindVertexArray(vao_); }

void Mesh::drawRange(const DrawRange& r) const {
    glDrawElements(GL_TRIANGLES, r.indexCount, GL_UNSIGNED_INT,
                   reinterpret_cast<const void*>(static_cast<uintptr_t>(r.firstIndex) * sizeof(uint32_t)));
}

} // namespace k
