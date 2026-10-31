#include "render/renderer.h"

#include "core/cvar.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "core/png.h"
#include "core/time.h"
#include "platform/window.h"
#include "render/debug_draw.h"

#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>

namespace k {
namespace {

ConVar mat_wireframe("mat_wireframe", "0", FCVAR_CHEAT, "Draw the world in wireframe.");
ConVar mat_fullbright("mat_fullbright", "0", FCVAR_CHEAT, "Disable lighting.");
ConVar mat_antialias("mat_antialias", "4", FCVAR_ARCHIVE, "MSAA samples (0, 2, 4 or 8).", 0.0f, 8.0f);
ConVar mat_exposure("mat_exposure", "1", FCVAR_CHEAT, "Tonemapping exposure.", 0.01f, 16.0f);
ConVar mat_tonemap("mat_tonemap", "1", FCVAR_NONE, "0 = clamp, 1 = ACES filmic.");
ConVar r_drawworld("r_drawworld", "1", FCVAR_CHEAT, "Draw world geometry.");
ConVar r_drawprops("r_drawprops", "1", FCVAR_CHEAT, "Draw props.");
ConVar r_drawsky("r_drawsky", "1", FCVAR_CHEAT, "Draw the skybox.");
ConVar r_showbounds("r_showbounds", "0", FCVAR_CHEAT, "Draw bounding boxes around props.");
ConVar r_drawaxis("r_drawaxis", "0", FCVAR_CHEAT, "Draw the world origin axes.");
ConVar r_fog("r_fog", "1", FCVAR_NONE, "Distance fog.");
ConVar r_fogdensity("r_fogdensity", "0.00018", FCVAR_NONE, "Fog density.");
ConVar r_nearz("r_nearz", "4", FCVAR_CHEAT, "Near clip plane distance.");
ConVar r_farz("r_farz", "16384", FCVAR_CHEAT, "Far clip plane distance.");
ConVar r_skyname("r_skyname", "sky_day", FCVAR_NONE, "Sky cubemap name (materials/skybox/<name>_px.png ...).");

const Vec3 kFogColor{0.60f, 0.70f, 0.82f};

Vec3 lightColor(const bsp::Entity& e, const char* key, float defBrightness) {
    Vec4 v(255, 255, 255, defBrightness);
    auto s = bsp::entityString(e, key);
    if (!s.empty()) {
        float c[4] = {255, 255, 255, defBrightness};
        int n = std::sscanf(s.c_str(), "%f %f %f %f", &c[0], &c[1], &c[2], &c[3]);
        if (n >= 3) v = Vec4(c[0], c[1], c[2], n == 4 ? c[3] : defBrightness);
    }
    return glm::pow(Vec3(v) / 255.0f, Vec3(2.2f)) * (v.w / 255.0f);
}

struct LineVertex {
    Vec3 position;
    Vec3 color;
};

void GLAPIENTRY glDebugCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message,
                                const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    if (type == GL_DEBUG_TYPE_ERROR)
        log::error("GL: {}", message);
    else
        log::debug("GL: {}", message);
}

Mat4 propMatrix(const StaticProp& p) {
    Mat4 m = glm::translate(Mat4(1.0f), p.origin);
    m = glm::rotate(m, deg2rad(p.angles.yaw), Vec3(0, 0, 1));
    m = glm::rotate(m, deg2rad(p.angles.pitch), Vec3(0, 1, 0));
    m = glm::rotate(m, deg2rad(p.angles.roll), Vec3(1, 0, 0));
    return glm::scale(m, Vec3(p.scale));
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

    materials_.init(&textures_);
    models_.init(&materials_);

    glCreateVertexArrays(1, &emptyVao_);

    // Sky: a unit cube drawn around the camera.
    const float s = 1.0f;
    const float cube[] = {
        -s, -s, -s, s, -s, -s, s, s, -s, -s, -s, -s, s, s, -s, -s, s, -s, // -Z
        -s, -s, s, s, s, s, s, -s, s, -s, -s, s, -s, s, s, s, s, s,       // +Z
        -s, -s, -s, -s, s, -s, -s, s, s, -s, -s, -s, -s, s, s, -s, -s, s, // -X
        s, -s, -s, s, s, s, s, s, -s, s, -s, -s, s, -s, s, s, s, s,       // +X
        -s, -s, -s, -s, -s, s, s, -s, s, -s, -s, -s, s, -s, s, s, -s, -s, // -Y
        -s, s, -s, s, s, s, -s, s, s, -s, s, -s, s, s, -s, s, s, s,       // +Y
    };
    glCreateVertexArrays(1, &skyVao_);
    glCreateBuffers(1, &skyVbo_);
    glNamedBufferData(skyVbo_, sizeof(cube), cube, GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(skyVao_, 0, skyVbo_, 0, 3 * sizeof(float));
    glEnableVertexArrayAttrib(skyVao_, 0);
    glVertexArrayAttribFormat(skyVao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(skyVao_, 0, 0);

    glCreateVertexArrays(1, &lineVao_);
    glCreateBuffers(1, &lineVbo_);
    glVertexArrayVertexBuffer(lineVao_, 0, lineVbo_, 0, sizeof(LineVertex));
    for (GLuint i = 0; i < 2; ++i) {
        glEnableVertexArrayAttrib(lineVao_, i);
        glVertexArrayAttribFormat(lineVao_, i, 3, GL_FLOAT, GL_FALSE, i == 0 ? 0 : sizeof(Vec3));
        glVertexArrayAttribBinding(lineVao_, i, 0);
    }

    // Compile everything up front so errors show at startup.
    bool ok = true;
    for (const char* name : {"lit_generic", "unlit_generic", "lightmapped_generic", "sky", "debug_line", "postprocess"})
        ok &= shaders_.get(name) != nullptr;
    if (!ok) log::error("Some shaders failed to compile; see above");
    return shaders_.get("lit_generic") && shaders_.get("postprocess");
}

void Renderer::shutdown() {
    unloadMap();
    models_.clear();
    materials_.clear();
    textures_.clear();
    shaders_.clear();
    sceneTarget_.release();
    resolveTarget_.release();
    for (uint32_t* vao : {&emptyVao_, &skyVao_, &lineVao_})
        if (*vao) glDeleteVertexArrays(1, vao);
    for (uint32_t* buf : {&skyVbo_, &lineVbo_})
        if (*buf) glDeleteBuffers(1, buf);
    emptyVao_ = skyVao_ = lineVao_ = skyVbo_ = lineVbo_ = 0;
    window_ = nullptr;
}

bool Renderer::reloadShaders() { return shaders_.reloadAll(); }

void Renderer::reloadMaterials() {
    textures_.reloadAll();
    materials_.reloadAll();
}

void Renderer::loadMap(std::shared_ptr<const bsp::BspData> bsp) {
    unloadMap();
    world_ = std::make_unique<WorldRenderer>();
    world_->load(bsp, materials_, textures_);

    // Static props. They become real entities in Phase 5.
    for (auto& e : bsp->entities) {
        if (bsp::entityString(e, "classname") != "prop_static") continue;
        Model* m = models_.find(bsp::entityString(e, "model"));
        if (!m) continue;
        Vec3 ang = bsp::entityVec3(e, "angles");
        props_.push_back({m, bsp::entityVec3(e, "origin"), {ang.x, ang.y, ang.z}, bsp::entityFloat(e, "modelscale", 1.0f)});
    }

    if (auto* env = bsp->findEntity("light_environment")) {
        Vec3 ang = bsp::entityVec3(*env, "angles");
        float pitch = env->count("pitch") ? bsp::entityFloat(*env, "pitch") : ang.x;
        angleVectors({pitch, ang.y, 0}, &sunDir_);
        sunColor_ = lightColor(*env, "_light", 200);
        Vec3 amb = lightColor(*env, "_ambient", 40);
        ambientSky_ = amb;
        ambientGround_ = amb * 0.35f + sunColor_ * 0.08f; // rough stand-in for bounced light
    }

    std::string skyName = r_skyname.getString();
    if (!bsp->entities.empty()) skyName = bsp::entityString(bsp->entities[0], "skyname", skyName);
    sky_ = textures_.findCube(skyName);
}

void Renderer::unloadMap() {
    world_.reset();
    props_.clear();
    sky_ = nullptr;
}

void Renderer::ensureTargets() {
    int samples = mat_antialias.getInt();
    samples = samples >= 8 ? 8 : samples >= 4 ? 4 : samples >= 2 ? 2 : 1;
    if (!sceneTarget_.matches(width_, height_, samples)) {
        if (!sceneTarget_.create(width_, height_, samples, true) && samples > 1) {
            log::warn("MSAA x{} not supported, disabling", samples);
            mat_antialias.setValue(0);
            sceneTarget_.create(width_, height_, 1, true);
        }
    }
    if (!resolveTarget_.matches(width_, height_, 1)) resolveTarget_.create(width_, height_, 1, false);
}

void Renderer::beginFrame() {
    window_->drawableSize(width_, height_);
    stats_ = {};
    shaders_.pollChanges(platformTime());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawMesh(const Mesh& mesh, const Mat4& model, bool translucentPass) {
    mesh.bindVertexArray();
    for (auto& range : mesh.ranges()) {
        const Material& mat = *range.material;
        if (mat.translucent != translucentPass) continue;
        Shader* shader = shaders_.get(mat.shaderFile());
        if (!shader) continue;
        shader->bind();
        shader->set("u_viewProj", viewProj_);
        shader->set("u_model", model);
        shader->set("u_cameraPos", cameraPos_);
        shader->set("u_sunDir", sunDir_);
        shader->set("u_sunColor", sunColor_);
        shader->set("u_ambientSky", ambientSky_);
        shader->set("u_ambientGround", ambientGround_);
        shader->set("u_fogColor", kFogColor);
        shader->set("u_fogDensity", r_fog.getBool() ? r_fogdensity.getFloat() : 0.0f);
        shader->set("u_fullbright", mat_fullbright.getInt());
        materials_.bind(mat, *shader);
        if (mat.noCull) glDisable(GL_CULL_FACE);
        mesh.drawRange(range);
        if (mat.noCull) glEnable(GL_CULL_FACE);
        stats_.drawCalls++;
        stats_.triangles += static_cast<size_t>(range.indexCount) / 3;
    }
}

void Renderer::renderView(const ViewSetup& view) {
    if (width_ <= 0 || height_ <= 0) return;
    ensureTargets();
    sceneTarget_.bind();
    glClearColor(kFogColor.x, kFogColor.y, kFogColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    // Source treats fov as horizontal at 4:3 and widens it for wider screens ("Hor+").
    float vfov = 2.0f * std::atan(std::tan(deg2rad(view.fov) * 0.5f) * 0.75f);
    Mat4 proj = glm::perspective(vfov, aspect, r_nearz.getFloat(), r_farz.getFloat());
    Vec3 forward, right, up;
    angleVectors(view.angles, &forward, &right, &up);
    Mat4 viewMat = glm::lookAt(view.origin, view.origin + forward, up);
    viewProj_ = proj * viewMat;
    cameraPos_ = view.origin;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, mat_wireframe.getBool() ? GL_LINE : GL_FILL);

    WorldRenderer::DrawParams wp{viewProj_, cameraPos_, kFogColor,
                                 r_fog.getBool() ? r_fogdensity.getFloat() : 0.0f, mat_fullbright.getBool(), false};

    // Opaque
    if (world_ && r_drawworld.getBool()) {
        world_->draw(wp, shaders_, materials_);
        stats_.drawCalls += world_->stats().drawCalls;
        stats_.triangles += world_->stats().triangles;
    }
    if (r_drawprops.getBool())
        for (auto& p : props_) drawMesh(*p.model->mesh, propMatrix(p), false);

    // Sky fills whatever is left (drawn at the far plane).
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if (sky_ && r_drawsky.getBool()) drawSky(proj * Mat4(glm::mat3(viewMat)));

    // Translucent (unsorted for now; sorted back-to-front once entities exist)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    if (world_ && r_drawworld.getBool()) {
        wp.translucentPass = true;
        world_->draw(wp, shaders_, materials_);
    }
    if (r_drawprops.getBool())
        for (auto& p : props_) drawMesh(*p.model->mesh, propMatrix(p), true);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // Debug overlays
    if (r_drawaxis.getBool()) debugdraw::axis(Vec3(0, 0, 1), 64.0f);
    if (r_showbounds.getBool())
        for (auto& p : props_)
            debugdraw::box(p.origin + p.model->mesh->mins() * p.scale, p.origin + p.model->mesh->maxs() * p.scale,
                           {1, 0.5f, 0});
    drawDebugLines(viewProj_);
    debugdraw::prune(platformTime());

    postProcess();
}

void Renderer::drawSky(const Mat4& viewProjNoTranslation) {
    Shader* shader = shaders_.get("sky");
    if (!shader) return;
    shader->bind();
    shader->set("u_viewProj", viewProjNoTranslation);
    shader->set("u_sky", 0);
    sky_->bind(0);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glEnable(GL_CULL_FACE);
    glDepthFunc(GL_LESS);
    stats_.drawCalls++;
}

void Renderer::drawDebugLines(const Mat4& viewProj) {
    const auto& lines = debugdraw::lines();
    Shader* shader = shaders_.get("debug_line");
    if (lines.empty() || !shader) return;
    std::vector<LineVertex> depth, noDepth;
    for (auto& l : lines) {
        auto& dst = l.depthTest ? depth : noDepth;
        dst.push_back({l.a, l.color});
        dst.push_back({l.b, l.color});
    }
    shader->bind();
    shader->set("u_viewProj", viewProj);
    glBindVertexArray(lineVao_);
    for (int pass = 0; pass < 2; ++pass) {
        auto& v = pass == 0 ? depth : noDepth;
        if (v.empty()) continue;
        if (pass == 1) glDisable(GL_DEPTH_TEST);
        glNamedBufferData(lineVbo_, static_cast<GLsizeiptr>(v.size() * sizeof(LineVertex)), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(v.size()));
        stats_.drawCalls++;
    }
    glEnable(GL_DEPTH_TEST);
}

void Renderer::postProcess() {
    sceneTarget_.blitTo(resolveTarget_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    Shader* shader = shaders_.get("postprocess");
    if (!shader) return;
    shader->bind();
    shader->set("u_scene", 0);
    shader->set("u_exposure", mat_exposure.getFloat());
    shader->set("u_tonemap", mat_tonemap.getInt());
    glBindTextureUnit(0, resolveTarget_.colorTexture());
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::endFrame() {
    if (screenshotPending_) {
        screenshotPending_ = false;
        screenshot();
    }
}

std::string Renderer::screenshot() {
    if (width_ <= 0 || height_ <= 0) return {};
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::vector<uint8_t> pixels(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    // OpenGL rows are bottom-up; PNG wants top-down.
    const size_t stride = static_cast<size_t>(width_) * 3;
    for (int y = 0; y < height_ / 2; ++y)
        std::swap_ranges(pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * stride),
                         pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y + 1) * stride),
                         pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(height_ - 1 - y) * stride));
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
