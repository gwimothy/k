#include "render/renderer.h"

#include "core/cvar.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "core/png.h"
#include "platform/window.h"

#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>

namespace k {
namespace {

ConVar mat_wireframe("mat_wireframe", "0", FCVAR_CHEAT, "Draw the world in wireframe.");
ConVar r_drawworld("r_drawworld", "1", FCVAR_CHEAT, "Draw world geometry.");
ConVar r_fog("r_fog", "1", FCVAR_NONE, "Distance fog.");
ConVar r_fogdensity("r_fogdensity", "0.00025", FCVAR_NONE, "Fog density.");
ConVar r_nearz("r_nearz", "4", FCVAR_CHEAT, "Near clip plane distance.");
ConVar r_farz("r_farz", "16384", FCVAR_CHEAT, "Far clip plane distance.");
ConVar mat_fullbright("mat_fullbright", "0", FCVAR_CHEAT, "Disable lighting.");

const Vec3 kSkyColor{0.47f, 0.60f, 0.78f};

void GLAPIENTRY glDebugCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message,
                                const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    if (type == GL_DEBUG_TYPE_ERROR)
        log::error("GL: {}", message);
    else
        log::debug("GL: {}", message);
}

} // namespace

bool Renderer::init(Window& window) {
    window_ = &window;
    int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
    if (!version) {
        log::error("Failed to load OpenGL functions");
        return false;
    }
    glRendererName_ = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    log::info("OpenGL {}.{} - {}", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version), glRendererName_);
    if (GLAD_VERSION_MAJOR(version) < 4 || (GLAD_VERSION_MAJOR(version) == 4 && GLAD_VERSION_MINOR(version) < 5)) {
        log::error("OpenGL 4.5 is required");
        return false;
    }

    GLint flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugCallback, nullptr);
    }

    return reloadShaders();
}

void Renderer::shutdown() {
    worldMesh_.reset();
    worldShader_ = Shader();
    window_ = nullptr;
}

bool Renderer::reloadShaders() {
    Shader s;
    if (!s.load("world")) return false;
    worldShader_ = std::move(s);
    return true;
}

void Renderer::loadMap(const std::string& name) {
    if (name != "devroom") log::warn("Map loading arrives in Phase 3; showing devroom instead of '{}'", name);
    worldMesh_ = std::make_unique<Mesh>();
    worldMesh_->upload(buildDevRoom());
}

void Renderer::unloadMap() { worldMesh_.reset(); }

void Renderer::beginFrame() {
    window_->drawableSize(width_, height_);
    stats_ = {};
    glViewport(0, 0, width_, height_);
    glClearColor(kSkyColor.x, kSkyColor.y, kSkyColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::renderView(const ViewSetup& view) {
    if (!worldMesh_ || !worldShader_.valid() || width_ <= 0 || height_ <= 0) return;

    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    // Source treats fov as horizontal at 4:3 and widens it for wider screens ("Hor+").
    float vfov = 2.0f * std::atan(std::tan(deg2rad(view.fov) * 0.5f) * 0.75f);
    Mat4 proj = glm::perspective(vfov, aspect, r_nearz.getFloat(), r_farz.getFloat());
    Vec3 forward, right, up;
    angleVectors(view.angles, &forward, &right, &up);
    Mat4 viewMat = glm::lookAt(view.origin, view.origin + forward, up);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glPolygonMode(GL_FRONT_AND_BACK, mat_wireframe.getBool() ? GL_LINE : GL_FILL);

    worldShader_.bind();
    worldShader_.set("u_viewProj", proj * viewMat);
    worldShader_.set("u_model", Mat4(1.0f));
    worldShader_.set("u_cameraPos", view.origin);
    worldShader_.set("u_sunDir", glm::normalize(Vec3(0.35f, 0.25f, 0.9f)));
    worldShader_.set("u_fogColor", kSkyColor);
    worldShader_.set("u_fogDensity", r_fog.getBool() ? r_fogdensity.getFloat() : 0.0f);
    worldShader_.set("u_fullbright", mat_fullbright.getInt());

    if (r_drawworld.getBool()) {
        worldMesh_->draw();
        stats_.drawCalls++;
        stats_.triangles += worldMesh_->triangleCount();
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void Renderer::endFrame() {
    if (screenshotPending_) {
        screenshotPending_ = false;
        screenshot();
    }
}

std::string Renderer::screenshot() {
    if (width_ <= 0 || height_ <= 0) return {};
    std::vector<uint8_t> pixels(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    // OpenGL rows are bottom-up; PNG wants top-down.
    const size_t stride = static_cast<size_t>(width_) * 3;
    for (int y = 0; y < height_ / 2; ++y)
        std::swap_ranges(pixels.begin() + static_cast<std::ptrdiff_t>(y * stride),
                         pixels.begin() + static_cast<std::ptrdiff_t>((y + 1) * stride),
                         pixels.begin() + static_cast<std::ptrdiff_t>((height_ - 1 - y) * stride));
    auto png = png::encode(pixels, width_, height_, 3);

    std::string name;
    for (int i = 0; i < 10000; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "screenshots/k%04d.png", i);
        auto full = fs::writePath(buf);
        if (full && !std::filesystem::exists(*full)) {
            name = buf;
            break;
        }
    }
    if (name.empty() || !fs::writeBinary(name, png.data(), png.size())) {
        log::warn("Couldn't write screenshot");
        return {};
    }
    log::info("Wrote {}", name);
    return name;
}

} // namespace k
