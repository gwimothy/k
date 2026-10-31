#include "render/texture.h"

#include "core/cvar.h"
#include "core/filesystem.h"
#include "core/log.h"

#include <glad/gl.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace k {
namespace {

ConVar mat_anisotropy("mat_anisotropy", "8", FCVAR_ARCHIVE, "Anisotropic filtering level (applies to newly loaded textures).", 1.0f, 16.0f);

const char* const kExtensions[] = {".png", ".tga", ".jpg"};

// Core in GL 4.6 / ARB_texture_filter_anisotropic; supported by every 4.5 driver in practice.
constexpr GLenum kTextureMaxAnisotropy = 0x84FE;

} // namespace

bool decodeImage(const uint8_t* data, size_t size, int& width, int& height,
                 std::unique_ptr<uint8_t[], void (*)(void*)>& out) {
    int channels = 0;
    uint8_t* px = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!px) return false;
    out = std::unique_ptr<uint8_t[], void (*)(void*)>(px, stbi_image_free);
    return true;
}

Texture::~Texture() { release(); }

void Texture::release() {
    if (id_) glDeleteTextures(1, &id_);
    id_ = 0;
}

void Texture::create2D(int width, int height, const uint8_t* rgba, TextureKind kind, bool mipmaps) {
    release();
    target_ = GL_TEXTURE_2D;
    width_ = width;
    height_ = height;
    int levels = mipmaps ? 1 + static_cast<int>(std::floor(std::log2(std::max(width, height)))) : 1;
    glCreateTextures(GL_TEXTURE_2D, 1, &id_);
    glTextureStorage2D(id_, levels, kind == TextureKind::Color ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(id_, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (mipmaps) glGenerateTextureMipmap(id_);
    glTextureParameteri(id_, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTextureParameteri(id_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(id_, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(id_, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameterf(id_, kTextureMaxAnisotropy, mat_anisotropy.getFloat());
}

void Texture::createCube(int size, const uint8_t* const faces[6], TextureKind kind) {
    release();
    target_ = GL_TEXTURE_CUBE_MAP;
    width_ = height_ = size;
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &id_);
    glTextureStorage2D(id_, 1, kind == TextureKind::Color ? GL_SRGB8_ALPHA8 : GL_RGBA8, size, size);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int f = 0; f < 6; ++f)
        glTextureSubImage3D(id_, 0, 0, 0, f, size, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, faces[f]);
    glTextureParameteri(id_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(id_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (GLenum w : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
        glTextureParameteri(id_, w, GL_CLAMP_TO_EDGE);
}

void Texture::bind(int unit) const { glBindTextureUnit(static_cast<GLuint>(unit), id_); }

// ---- TextureManager ------------------------------------------------------------------

Texture* TextureManager::errorTexture() {
    if (!error_) {
        // Source's famous missing texture: magenta/black checker.
        constexpr int n = 64;
        std::vector<uint8_t> px(n * n * 4);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                bool m = ((x / 32) + (y / 32)) % 2 == 0;
                uint8_t* p = &px[static_cast<size_t>((y * n + x) * 4)];
                p[0] = m ? 255 : 0; p[1] = 0; p[2] = m ? 255 : 0; p[3] = 255;
            }
        error_ = std::make_unique<Texture>();
        error_->create2D(n, n, px.data(), TextureKind::Color);
    }
    return error_.get();
}

Texture* TextureManager::whiteTexture() {
    if (!white_) {
        uint8_t px[4] = {255, 255, 255, 255};
        white_ = std::make_unique<Texture>();
        white_->create2D(1, 1, px, TextureKind::Color, false);
    }
    return white_.get();
}

Texture* TextureManager::flatNormalTexture() {
    if (!flatNormal_) {
        uint8_t px[4] = {128, 128, 255, 255};
        flatNormal_ = std::make_unique<Texture>();
        flatNormal_->create2D(1, 1, px, TextureKind::Data, false);
    }
    return flatNormal_.get();
}

bool TextureManager::load(Texture& tex, std::string_view name, TextureKind kind) {
    for (const char* ext : kExtensions) {
        auto data = fs::readBinary("materials/" + std::string(name) + ext);
        if (!data) continue;
        int w = 0, h = 0;
        std::unique_ptr<uint8_t[], void (*)(void*)> px(nullptr, stbi_image_free);
        if (!decodeImage(data->data(), data->size(), w, h, px)) {
            log::warn("Couldn't decode texture '{}{}': {}", name, ext, stbi_failure_reason());
            return false;
        }
        tex.create2D(w, h, px.get(), kind);
        return true;
    }
    return false;
}

Texture* TextureManager::find(std::string_view name, TextureKind kind) {
    std::string key(name);
    if (auto it = textures_.find(key); it != textures_.end())
        return it->second.missing ? errorTexture() : it->second.texture.get();
    Entry e{std::make_unique<Texture>(), kind};
    if (!load(*e.texture, name, kind)) {
        log::warn("Missing texture 'materials/{}'", name);
        e.missing = true;
    }
    auto* result = e.missing ? errorTexture() : e.texture.get();
    textures_.emplace(std::move(key), std::move(e));
    return result;
}

bool TextureManager::loadCube(Texture& tex, std::string_view name) {
    static const char* const suffix[6] = {"px", "nx", "py", "ny", "pz", "nz"};
    std::vector<std::unique_ptr<uint8_t[], void (*)(void*)>> faces;
    int size = 0;
    for (int f = 0; f < 6; ++f) {
        auto data = fs::readBinary("materials/skybox/" + std::string(name) + "_" + suffix[f] + ".png");
        int w = 0, h = 0;
        std::unique_ptr<uint8_t[], void (*)(void*)> px(nullptr, stbi_image_free);
        if (!data || !decodeImage(data->data(), data->size(), w, h, px) || w != h || (size && w != size)) {
            log::warn("Sky '{}': face {} missing or not square/same size", name, suffix[f]);
            return false;
        }
        size = w;
        faces.push_back(std::move(px));
    }
    const uint8_t* ptrs[6];
    for (int f = 0; f < 6; ++f) ptrs[f] = faces[static_cast<size_t>(f)].get();
    tex.createCube(size, ptrs, TextureKind::Color);
    return true;
}

Texture* TextureManager::findCube(std::string_view name) {
    std::string key = "skybox:" + std::string(name);
    if (auto it = textures_.find(key); it != textures_.end())
        return it->second.missing ? nullptr : it->second.texture.get();
    Entry e{std::make_unique<Texture>(), TextureKind::Color, true};
    e.missing = !loadCube(*e.texture, name);
    auto* result = e.missing ? nullptr : e.texture.get();
    textures_.emplace(std::move(key), std::move(e));
    return result;
}

void TextureManager::reloadAll() {
    for (auto& [name, e] : textures_) {
        if (e.cube)
            e.missing = !loadCube(*e.texture, std::string_view(name).substr(7));
        else
            e.missing = !load(*e.texture, name, e.kind);
    }
    log::info("Reloaded {} textures", textures_.size());
}

void TextureManager::clear() {
    textures_.clear();
    error_.reset();
    white_.reset();
    flatNormal_.reset();
}

} // namespace k
