#pragma once
// The renderer. Phase 1: one static test level ("devroom") lit by a sun.
// Phase 3 replaces the test geometry with compiled BSP maps.

#include "client/client.h"
#include "render/mesh.h"
#include "render/shader.h"

#include <filesystem>
#include <memory>
#include <string>

namespace k {

class Window;

struct FrameStats {
    size_t drawCalls = 0;
    size_t triangles = 0;
};

class Renderer {
public:
    bool init(Window& window);
    void shutdown();

    // Load the level geometry for a map. Unknown maps fall back to the devroom.
    void loadMap(const std::string& name);
    void unloadMap();

    void beginFrame();
    void renderView(const ViewSetup& view);
    void endFrame();

    // Reads back the current framebuffer and writes it as PNG through the filesystem.
    // Returns the path written (relative to the game dir) or empty on failure.
    std::string screenshot();
    void requestScreenshot() { screenshotPending_ = true; }

    bool reloadShaders();
    const FrameStats& stats() const { return stats_; }
    const char* glRenderer() const { return glRendererName_.c_str(); }

private:
    Window* window_ = nullptr;
    Shader worldShader_;
    std::unique_ptr<Mesh> worldMesh_;
    FrameStats stats_;
    int width_ = 0, height_ = 0;
    bool screenshotPending_ = false;
    std::string glRendererName_;
};

// Geometry for the built-in test level, exposed for tests and tools.
MeshData buildDevRoom();

} // namespace k
