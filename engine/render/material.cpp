#include "render/material.h"

#include "core/cvar.h"
#include "core/filesystem.h"
#include "core/keyvalues.h"
#include "core/log.h"
#include "render/shader.h"
#include "render/texture.h"

#include <algorithm>
#include <cctype>

namespace k {

namespace {
ConVar mat_showmissing("mat_showmissing", "1", FCVAR_NONE, "Log a warning for each missing material.");

std::string normalizeName(std::string_view name) {
    std::string s(name);
    std::replace(s.begin(), s.end(), '\\', '/');
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    if (s.starts_with("materials/")) s = s.substr(10);
    for (const char* ext : {".kmt", ".png", ".tga", ".jpg"})
        if (s.ends_with(ext)) s.resize(s.size() - std::char_traits<char>::length(ext));
    return s;
}
} // namespace

Material* MaterialSystem::errorMaterial() {
    if (!error_) {
        error_ = std::make_unique<Material>();
        error_->name = "__error";
        error_->baseTexture = textures_->errorTexture();
        error_->isError = true;
        error_->textureScale = 8.0f; // small checks so it reads as "error" on any size surface
    }
    return error_.get();
}

bool MaterialSystem::load(Material& m) {
    // Defaults, so reloading a material resets removed keys.
    std::string name = m.name;
    m = Material{};
    m.name = name;

    auto text = fs::readText("materials/" + name + ".kmt");
    if (!text) {
        // No material file: fall back to a texture of the same name.
        if (fs::exists("materials/" + name + ".png") || fs::exists("materials/" + name + ".tga") ||
            fs::exists("materials/" + name + ".jpg")) {
            m.baseTexture = textures_->find(name);
            return true;
        }
        return false;
    }

    std::string error;
    auto kv = KeyValues::parse(*text, &error);
    if (!kv || kv->children().empty() || !kv->children()[0].isSection()) {
        log::warn("Material '{}': {}", name, error.empty() ? "expected \"ShaderName\" { ... }" : error);
        return false;
    }
    const KeyValues& root = kv->children()[0];
    if (iequals(root.name(), "UnlitGeneric"))
        m.shader = MaterialShader::UnlitGeneric;
    else if (!iequals(root.name(), "LitGeneric"))
        log::warn("Material '{}': unknown shader '{}', using LitGeneric", name, root.name());

    std::string base = root.getString("$basetexture");
    m.baseTexture = base.empty() ? textures_->whiteTexture() : textures_->find(normalizeName(base));
    m.color = root.getVec3("$color", Vec3(1.0f));
    m.textureScale = root.getFloat("$texscale", 1.0f);
    m.alpha = root.getFloat("$alpha", 1.0f);
    m.translucent = root.getBool("$translucent") || m.alpha < 1.0f;
    m.alphaTest = root.getBool("$alphatest");
    m.alphaTestRef = root.getFloat("$alphatestreference", 0.5f);
    m.noCull = root.getBool("$nocull");
    m.selfIllum = root.getBool("$selfillum");
    m.surfaceProp = root.getString("$surfaceprop", "default");
    return true;
}

Material* MaterialSystem::find(std::string_view nameIn) {
    std::string name = normalizeName(nameIn);
    if (auto it = materials_.find(name); it != materials_.end()) return it->second.get();
    auto m = std::make_unique<Material>();
    m->name = name;
    if (!load(*m)) {
        if (mat_showmissing.getBool()) log::warn("Missing material '{}'", name);
        *m = *errorMaterial();
        m->name = name;
    }
    auto* ptr = m.get();
    materials_.emplace(name, std::move(m));
    return ptr;
}

void MaterialSystem::bind(const Material& m, Shader& shader) const {
    (m.baseTexture ? m.baseTexture : textures_->whiteTexture())->bind(0);
    shader.set("u_baseTexture", 0);
    shader.set("u_color", Vec4(m.color, m.alpha));
    shader.set("u_uvScale", m.textureScale);
    shader.set("u_alphaTest", m.alphaTest ? m.alphaTestRef : -1.0f);
    shader.set("u_selfIllum", m.selfIllum ? 1 : 0);
}

void MaterialSystem::reloadAll() {
    for (auto& [name, m] : materials_) {
        // Pointers handed out stay valid: we reload in place.
        if (!load(*m)) {
            *m = *errorMaterial();
            m->name = name;
        }
    }
    log::info("Reloaded {} materials", materials_.size());
}

void MaterialSystem::clear() {
    materials_.clear();
    error_.reset();
}

} // namespace k
