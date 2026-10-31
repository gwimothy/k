#pragma once
// Draws a compiled map: lightmapped faces, culled per leaf with the PVS and the view frustum.

#include "shared/bspfile.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace k {

struct Material;
class MaterialSystem;
class Shader;
class ShaderLibrary;
class TextureManager;

struct WorldStats {
    size_t visibleLeaves = 0;
    size_t visibleFaces = 0;
    size_t drawCalls = 0;
    size_t triangles = 0;
    int cameraLeaf = -1;
};

class WorldRenderer {
public:
    WorldRenderer() = default;
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;
    ~WorldRenderer();

    void load(std::shared_ptr<const bsp::BspData> bsp, MaterialSystem& materials, TextureManager& textures);

    struct DrawParams {
        Mat4 viewProj;
        Vec3 cameraPos;
        Vec3 fogColor;
        float fogDensity;
        bool fullbright;
        bool translucentPass;
    };
    void draw(const DrawParams& p, ShaderLibrary& shaders, MaterialSystem& materials);

    // Brush entities (doors, platforms): model number and how far they've moved.
    struct BrushInstance {
        int model;
        Vec3 origin;
    };
    void drawBrushModels(const DrawParams& p, const std::vector<BrushInstance>& instances, ShaderLibrary& shaders,
                         MaterialSystem& materials);

    const WorldStats& stats() const { return stats_; }
    const bsp::BspData* bsp() const { return bsp_.get(); }

private:
    void updateVisibility(const Mat4& viewProj, const Vec3& cameraPos);

    std::shared_ptr<const bsp::BspData> bsp_;
    uint32_t vao_ = 0, vbo_ = 0, ebo_ = 0, lightmap_ = 0, whiteLightmap_ = 0;
    std::vector<Material*> faceMaterials_;
    std::vector<uint32_t> faceVisFrame_;
    std::vector<uint8_t> pvs_;
    int pvsLeaf_ = -2;
    uint32_t frame_ = 0;

    // Per material: lists for glMultiDrawElements.
    struct Batch {
        Material* material;
        std::vector<int32_t> counts;
        std::vector<const void*> offsets;
    };
    std::vector<Batch> batches_;
    void drawBatches(const DrawParams& p, const Mat4& model, const std::vector<Batch>& batches, ShaderLibrary& shaders,
                     MaterialSystem& materials);
    WorldStats stats_;
};

} // namespace k
