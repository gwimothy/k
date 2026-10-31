#include "render/effects.h"

#include "core/cvar.h"
#include "render/shader_library.h"
#include "shared/protocol.h"

#include <glad/gl.h>

#include <algorithm>

namespace k {
namespace {

ConVar r_decals("r_decals", "128", FCVAR_ARCHIVE, "Maximum impact decals.", 0.0f, 1024.0f);
ConVar r_particles("r_particles", "1", FCVAR_ARCHIVE, "Draw particles.");

} // namespace

Effects::~Effects() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

void Effects::ensureBuffers() {
    if (vao_) return;
    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &vbo_);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vertex));
    const GLint sizes[] = {3, 2, 4};
    const GLuint offsets[] = {offsetof(Vertex, position), offsetof(Vertex, uv), offsetof(Vertex, color)};
    for (GLuint i = 0; i < 3; ++i) {
        glEnableVertexArrayAttrib(vao_, i);
        glVertexArrayAttribFormat(vao_, i, sizes[i], GL_FLOAT, GL_FALSE, offsets[i]);
        glVertexArrayAttribBinding(vao_, i, 0);
    }
}

void Effects::emit(const Vec3& pos, const Vec3& dir, int count, const Vec4& color, float speed, float size, float life,
                   float gravity, bool additive) {
    std::uniform_real_distribution<float> u(-1.0f, 1.0f), u01(0.0f, 1.0f);
    for (int i = 0; i < count; ++i) {
        Vec3 jitter(u(rng_), u(rng_), u(rng_));
        Vec3 v = glm::normalize(dir + jitter * 0.8f) * speed * (0.4f + u01(rng_));
        float l = life * (0.6f + 0.8f * u01(rng_));
        particles_.push_back({pos, v, color, size * (0.6f + 0.8f * u01(rng_)), l, l, gravity, additive});
    }
}

void Effects::spawn(uint8_t type, const Vec3& pos, const Vec3& normalIn) {
    Vec3 n = glm::length(normalIn) > 0.01f ? glm::normalize(normalIn) : Vec3(0, 0, 1);
    switch (static_cast<proto::TempEntityType>(type)) {
        case proto::TempEntityType::Impact: {
            emit(pos + n, n, 10, Vec4(0.55f, 0.52f, 0.48f, 0.8f), 90.0f, 5.0f, 0.7f, 120.0f, false); // dust
            emit(pos + n, n, 6, Vec4(1.0f, 0.75f, 0.35f, 1.0f), 260.0f, 1.5f, 0.25f, 400.0f, true);  // sparks
            int maxDecals = r_decals.getInt();
            if (maxDecals <= 0) break;
            Vec3 helper = std::fabs(n.z) < 0.9f ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
            Vec3 t = glm::normalize(glm::cross(helper, n));
            Decal d{pos + n * 0.1f, n, t, glm::cross(n, t), 7.0f, std::uniform_real_distribution<float>(0, 100)(rng_)};
            if (decals_.size() < static_cast<size_t>(maxDecals)) {
                decals_.push_back(d);
            } else {
                decals_[nextDecal_ % decals_.size()] = d; // oldest goes first
                ++nextDecal_;
            }
            break;
        }
        case proto::TempEntityType::Blood:
            emit(pos, n, 14, Vec4(0.6f, 0.05f, 0.03f, 0.9f), 120.0f, 4.0f, 0.6f, 500.0f, false);
            break;
        case proto::TempEntityType::Spark:
            emit(pos, n, 12, Vec4(1.0f, 0.8f, 0.4f, 1.0f), 300.0f, 1.5f, 0.3f, 400.0f, true);
            break;
    }
}

void Effects::update(float dt) {
    for (auto& p : particles_) {
        p.life -= dt;
        p.velocity.z -= p.gravity * dt;
        p.velocity *= 1.0f - std::min(1.0f, 1.5f * dt); // drag
        p.position += p.velocity * dt;
    }
    std::erase_if(particles_, [](const Particle& p) { return p.life <= 0; });
}

void Effects::clear() {
    particles_.clear();
    decals_.clear();
    nextDecal_ = 0;
}

void Effects::draw(const Mat4& viewProj, const Vec3& right, const Vec3& up, ShaderLibrary& shaders) {
    if (decals_.empty() && particles_.empty()) return;
    Shader* shader = shaders.get("particle");
    if (!shader) return;
    ensureBuffers();
    std::vector<Vertex> verts;
    auto quad = [&](const Vec3& c, const Vec3& ax, const Vec3& ay, const Vec4& color) {
        Vertex v0{c - ax - ay, {0, 0}, color}, v1{c + ax - ay, {1, 0}, color};
        Vertex v2{c + ax + ay, {1, 1}, color}, v3{c - ax + ay, {0, 1}, color};
        verts.insert(verts.end(), {v0, v1, v2, v0, v2, v3});
    };

    shader->bind();
    shader->set("u_viewProj", viewProj);
    glBindVertexArray(vao_);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    // Decals: dark soft discs stuck to surfaces, pulled toward the camera in depth.
    if (!decals_.empty()) {
        verts.clear();
        for (auto& d : decals_) quad(d.position, d.tangent * d.size, d.bitangent * d.size, Vec4(0.08f, 0.07f, 0.06f, d.seed));
        glNamedBufferData(vbo_, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data(), GL_STREAM_DRAW);
        shader->set("u_mode", 1);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -4.0f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    if (r_particles.getBool() && !particles_.empty()) {
        for (int pass = 0; pass < 2; ++pass) {
            bool additive = pass == 1;
            verts.clear();
            for (auto& p : particles_) {
                if (p.additive != additive) continue;
                Vec4 c = p.color;
                c.w *= std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
                quad(p.position, right * p.size, up * p.size, c);
            }
            if (verts.empty()) continue;
            glNamedBufferData(vbo_, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data(), GL_STREAM_DRAW);
            shader->set("u_mode", 0);
            glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace k
