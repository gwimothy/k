#include "render/world_renderer.h"

#include "core/cvar.h"
#include "core/log.h"
#include "render/material.h"
#include "render/shader_library.h"
#include "render/texture.h"

#include <glad/gl.h>

#include <algorithm>
#include <array>

namespace k {
namespace {

ConVar r_novis("r_novis", "0", FCVAR_CHEAT, "Ignore the PVS (draw every leaf in the frustum).");
ConVar r_lockpvs("r_lockpvs", "0", FCVAR_CHEAT, "Freeze the PVS at the current leaf, then fly around to inspect it.");
ConVar r_nofrustum("r_nofrustum", "0", FCVAR_CHEAT, "Disable frustum culling of leaves.");
ConVar r_drawbrushmodels("r_drawbrushmodels", "1", FCVAR_CHEAT, "Draw brush entities.");

struct Frustum {
    std::array<Vec4, 6> planes;
    explicit Frustum(const Mat4& m) {
        // Gribb/Hartmann plane extraction (column-major glm matrices).
        Vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
        Vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
        Vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
        Vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);
        planes = {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row3 + row2, row3 - row2};
    }
    bool visible(const Vec3& mins, const Vec3& maxs) const {
        for (auto& p : planes) {
            Vec3 positive(p.x >= 0 ? maxs.x : mins.x, p.y >= 0 ? maxs.y : mins.y, p.z >= 0 ? maxs.z : mins.z);
            if (p.x * positive.x + p.y * positive.y + p.z * positive.z + p.w < 0) return false;
        }
        return true;
    }
};

} // namespace

WorldRenderer::~WorldRenderer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    for (uint32_t* b : {&vbo_, &ebo_})
        if (*b) glDeleteBuffers(1, b);
    for (uint32_t* t : {&lightmap_, &whiteLightmap_})
        if (*t) glDeleteTextures(1, t);
}

void WorldRenderer::load(std::shared_ptr<const bsp::BspData> bsp, MaterialSystem& materials, TextureManager&) {
    bsp_ = std::move(bsp);
    const auto& b = *bsp_;

    glCreateVertexArrays(1, &vao_);
    glCreateBuffers(1, &vbo_);
    glCreateBuffers(1, &ebo_);
    glNamedBufferData(vbo_, static_cast<GLsizeiptr>(b.vertices.size() * sizeof(bsp::Vertex)), b.vertices.data(), GL_STATIC_DRAW);
    glNamedBufferData(ebo_, static_cast<GLsizeiptr>(b.indices.size() * sizeof(uint32_t)), b.indices.data(), GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(bsp::Vertex));
    glVertexArrayElementBuffer(vao_, ebo_);
    struct Attr { GLint size; GLuint offset; };
    const Attr attrs[] = {{3, offsetof(bsp::Vertex, position)}, {3, offsetof(bsp::Vertex, normal)},
                          {2, offsetof(bsp::Vertex, uv)}, {2, offsetof(bsp::Vertex, lightmapUV)}};
    for (GLuint i = 0; i < 4; ++i) {
        glEnableVertexArrayAttrib(vao_, i);
        glVertexArrayAttribFormat(vao_, i, attrs[i].size, GL_FLOAT, GL_FALSE, attrs[i].offset);
        glVertexArrayAttribBinding(vao_, i, 0);
    }

    // Lightmap atlas (linear HDR half floats). Without lighting, a white 1x1 is used instead.
    const uint16_t white[3] = {0x3C00, 0x3C00, 0x3C00}; // 1.0h
    glCreateTextures(GL_TEXTURE_2D, 1, &whiteLightmap_);
    glTextureStorage2D(whiteLightmap_, 1, GL_RGB16F, 1, 1);
    glTextureSubImage2D(whiteLightmap_, 0, 0, 0, 1, 1, GL_RGB, GL_HALF_FLOAT, white);
    if (b.lightmapWidth > 0 && b.lightmapHeight > 0) {
        glCreateTextures(GL_TEXTURE_2D, 1, &lightmap_);
        glTextureStorage2D(lightmap_, 1, GL_RGB16F, b.lightmapWidth, b.lightmapHeight);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        glTextureSubImage2D(lightmap_, 0, 0, 0, b.lightmapWidth, b.lightmapHeight, GL_RGB, GL_HALF_FLOAT, b.lightmap.data());
        glTextureParameteri(lightmap_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(lightmap_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(lightmap_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(lightmap_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    faceMaterials_.clear();
    for (auto& f : b.faces) faceMaterials_.push_back(materials.find(b.materials[f.material]));
    faceVisFrame_.assign(b.faces.size(), 0);
    pvsLeaf_ = -2;
    log::info("World: {} faces, {} leaves, lightmap {}x{}", b.faces.size(), b.leaves.size(), b.lightmapWidth,
              b.lightmapHeight);
}

void WorldRenderer::updateVisibility(const Mat4& viewProj, const Vec3& cameraPos) {
    const auto& b = *bsp_;
    ++frame_;
    stats_ = {};
    int leaf = b.findLeaf(cameraPos);
    stats_.cameraLeaf = leaf;
    if (!r_lockpvs.getBool() && leaf != pvsLeaf_) {
        b.decompressVis(leaf, pvs_);
        pvsLeaf_ = leaf;
    }
    bool novis = r_novis.getBool() || pvs_.size() != b.leaves.size();
    // In a solid leaf (noclip in a wall) everything is potentially visible.
    if (leaf >= 0 && (b.leaves[static_cast<size_t>(leaf)].contents & bsp::CONTENTS_SOLID)) novis = true;
    Frustum frustum(viewProj);
    bool cull = !r_nofrustum.getBool();

    for (size_t l = 0; l < b.leaves.size(); ++l) {
        const auto& lf = b.leaves[l];
        if (lf.numLeafFaces == 0) continue;
        if (!novis && !pvs_[l]) continue;
        if (cull && !frustum.visible(lf.mins, lf.maxs)) continue;
        ++stats_.visibleLeaves;
        for (uint32_t i = 0; i < lf.numLeafFaces; ++i) {
            uint32_t f = b.leafFaces[lf.firstLeafFace + i];
            if (faceVisFrame_[f] == frame_) continue;
            faceVisFrame_[f] = frame_;
        }
    }
    // Brush entities are always drawn for now (they get their own transforms in Phase 5).
    if (r_drawbrushmodels.getBool())
        for (size_t m = 1; m < b.models.size(); ++m)
            for (uint32_t i = 0; i < b.models[m].numFaces; ++i) faceVisFrame_[b.models[m].firstFace + i] = frame_;

    // Batch visible faces by material.
    batches_.clear();
    std::vector<int> batchOf(faceMaterials_.size(), -1);
    for (size_t f = 0; f < b.faces.size(); ++f) {
        if (faceVisFrame_[f] != frame_) continue;
        Material* m = faceMaterials_[f];
        int bi = -1;
        for (size_t i = 0; i < batches_.size(); ++i)
            if (batches_[i].material == m) { bi = static_cast<int>(i); break; }
        if (bi < 0) {
            batches_.push_back({m, {}, {}});
            bi = static_cast<int>(batches_.size()) - 1;
        }
        const auto& face = b.faces[f];
        batches_[static_cast<size_t>(bi)].counts.push_back(static_cast<int32_t>(face.numIndices));
        batches_[static_cast<size_t>(bi)].offsets.push_back(
            reinterpret_cast<const void*>(static_cast<uintptr_t>(face.firstIndex) * sizeof(uint32_t)));
        ++stats_.visibleFaces;
        stats_.triangles += face.numIndices / 3;
    }
}

void WorldRenderer::draw(const DrawParams& p, ShaderLibrary& shaders, MaterialSystem& materials) {
    if (!bsp_) return;
    if (!p.translucentPass) updateVisibility(p.viewProj, p.cameraPos);
    glBindVertexArray(vao_);
    glBindTextureUnit(1, (lightmap_ && !p.fullbright) ? lightmap_ : whiteLightmap_);
    for (auto& batch : batches_) {
        const Material& mat = *batch.material;
        if (mat.translucent != p.translucentPass) continue;
        Shader* shader = shaders.get(mat.shader == MaterialShader::UnlitGeneric ? "unlit_generic" : "lightmapped_generic");
        if (!shader) continue;
        shader->bind();
        shader->set("u_viewProj", p.viewProj);
        shader->set("u_model", Mat4(1.0f));
        shader->set("u_cameraPos", p.cameraPos);
        shader->set("u_fogColor", p.fogColor);
        shader->set("u_fogDensity", p.fogDensity);
        shader->set("u_lightmap", 1);
        materials.bind(mat, *shader);
        if (mat.noCull) glDisable(GL_CULL_FACE);
        glMultiDrawElements(GL_TRIANGLES, batch.counts.data(), GL_UNSIGNED_INT, batch.offsets.data(),
                            static_cast<GLsizei>(batch.counts.size()));
        if (mat.noCull) glEnable(GL_CULL_FACE);
        ++stats_.drawCalls;
    }
}

} // namespace k
