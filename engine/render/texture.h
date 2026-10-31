#pragma once
// GPU textures and the texture cache.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace k {

enum class TextureKind {
    Color,  // sRGB (albedo, sky)
    Data,   // linear (normal maps, masks)
};

class Texture {
public:
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    ~Texture();

    // Upload RGBA8 pixels (top row first) with mipmaps.
    void create2D(int width, int height, const uint8_t* rgba, TextureKind kind, bool mipmaps = true);
    // Six RGBA8 faces in GL order (+X, -X, +Y, -Y, +Z, -Z), all the same size.
    void createCube(int size, const uint8_t* const faces[6], TextureKind kind);

    void bind(int unit) const;
    uint32_t id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void release();
    uint32_t id_ = 0;
    uint32_t target_ = 0;
    int width_ = 0, height_ = 0;
};

// Decodes PNG/TGA/JPEG from memory into RGBA8. Returns false on failure.
bool decodeImage(const uint8_t* data, size_t size, int& width, int& height, std::unique_ptr<uint8_t[], void (*)(void*)>& out);

class TextureManager {
public:
    // `name` is relative to materials/ without extension, e.g. "dev/dev_orange".
    // Missing textures return the magenta/black error texture (never null).
    Texture* find(std::string_view name, TextureKind kind = TextureKind::Color);
    // Loads a sky cubemap: materials/skybox/<name>_{px,nx,py,ny,pz,nz}.png.
    Texture* findCube(std::string_view name);

    Texture* errorTexture();
    Texture* whiteTexture();
    Texture* flatNormalTexture();

    // Re-read every cached texture from disk.
    void reloadAll();
    void clear();
    size_t count() const { return textures_.size(); }

private:
    struct Entry {
        std::unique_ptr<Texture> texture;
        TextureKind kind;
        bool cube = false;
        bool missing = false;
    };
    bool load(Texture& tex, std::string_view name, TextureKind kind);
    bool loadCube(Texture& tex, std::string_view name);

    std::unordered_map<std::string, Entry> textures_;
    std::unique_ptr<Texture> error_, white_, flatNormal_;
};

} // namespace k
