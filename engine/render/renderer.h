#pragma once
// The renderer.
//
// Frame: scene → HDR target (MSAA optional) → resolve → tonemap + gamma → window → UI.
// Phase 2 draws a built-in test level with materials, glTF props, a sky cubemap and debug
// lines. Phase 3 replaces the test level with compiled BSP maps.

#include "client/client.h"
#include "render/material.h"
#include "render/mesh.h"
#include "render/model.h"
#include "render/render_target.h"
#include "render/shader_library.h"
#include "render/texture.h"
#include "render/world_renderer.h"
#include "shared/bspfile.h"

#include <memory>
#include <string>
#include <vector>

namespace k {

class Window;

struct FrameStats {
    size_t drawCalls = 0;
    size_t triangles = 0;
};

// A model placed in the world. Becomes prop_static / prop_dynamic entities in Phase 5.
struct StaticProp {
    Model* model;
    Vec3 origin;
    QAngle angles;
    float scale = 1.0f;
};

class Renderer {
public:
    bool init(Window& window);
    void shutdown();

    // Set up rendering for a compiled map (world geometry, props, sky, lighting).
    void loadMap(std::shared_ptr<const bsp::BspData> bsp);
    void unloadMap();

    void beginFrame();
    void renderView(const ViewSetup& view);
    // Call after the UI has been drawn; handles pending screenshots.
    void endFrame();

    std::string screenshot();
    void requestScreenshot() { screenshotPending_ = true; }

    bool reloadShaders();
    void reloadMaterials();

    const FrameStats& stats() const { return stats_; }
    const WorldStats* worldStats() const { return world_ ? &world_->stats() : nullptr; }
    const std::string& glRenderer() const { return glRendererName_; }
    TextureManager& textures() { return textures_; }
    MaterialSystem& materials() { return materials_; }
    ModelManager& models() { return models_; }

private:
    void ensureTargets();
    void drawMesh(const Mesh& mesh, const Mat4& model, bool translucentPass);
    void drawSky(const Mat4& viewProjNoTranslation);
    void drawDebugLines(const Mat4& viewProj);
    void postProcess();

    Window* window_ = nullptr;
    TextureManager textures_;
    MaterialSystem materials_;
    ModelManager models_;
    ShaderLibrary shaders_;

    std::unique_ptr<WorldRenderer> world_;
    std::vector<StaticProp> props_;
    // Prop lighting from the map's light_environment (until per-leaf ambient samples exist).
    Vec3 sunDir_{0.35f, 0.25f, 0.9f}, sunColor_{1.9f, 1.75f, 1.5f};
    Vec3 ambientSky_{0.32f, 0.38f, 0.48f}, ambientGround_{0.14f, 0.13f, 0.12f};
    Texture* sky_ = nullptr;

    RenderTarget sceneTarget_;   // possibly multisampled
    RenderTarget resolveTarget_; // single-sample, read by the post pass
    uint32_t emptyVao_ = 0;      // for attribute-less fullscreen triangles
    uint32_t skyVao_ = 0, skyVbo_ = 0;
    uint32_t lineVao_ = 0, lineVbo_ = 0;

    Mat4 viewProj_{1.0f};
    Vec3 cameraPos_{0};
    FrameStats stats_;
    int width_ = 0, height_ = 0;
    bool screenshotPending_ = false;
    std::string glRendererName_;
};

} // namespace k
