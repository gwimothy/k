#pragma once
// Materials describe how a surface is drawn: which shader, which textures, which parameters.
// They live in base/materials/<name>.kmt using KeyValues (the same idea as Source's .vmt):
//
//   "LitGeneric"
//   {
//       "$basetexture"  "dev/dev_orange"
//       "$color"        "[1 1 1]"
//       "$surfaceprop"  "concrete"
//   }
//
// If no .kmt exists but a texture with the same name does, a LitGeneric material is made
// for it automatically. Anything else gets the magenta/black error material.

#include "shared/kmath.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace k {

class Shader;
class ShaderLibrary;
class Texture;
class TextureManager;

enum class MaterialShader { LitGeneric, UnlitGeneric };

struct Material {
    std::string name;
    MaterialShader shader = MaterialShader::LitGeneric;
    Texture* baseTexture = nullptr;
    Vec3 color{1.0f};
    float textureScale = 1.0f; // $texscale: >1 repeats the texture more often
    float alpha = 1.0f;
    bool translucent = false;
    bool alphaTest = false;
    float alphaTestRef = 0.5f;
    bool noCull = false;
    bool selfIllum = false;
    std::string surfaceProp = "default";
    bool isError = false;

    const char* shaderFile() const { return shader == MaterialShader::UnlitGeneric ? "unlit_generic" : "lit_generic"; }
};

class MaterialSystem {
public:
    void init(TextureManager* textures) { textures_ = textures; }

    // `name` is relative to materials/ without extension. Never returns null.
    Material* find(std::string_view name);
    Material* errorMaterial();

    // Bind a material's textures and parameters to an already-bound shader.
    void bind(const Material& m, Shader& shader) const;

    // Re-read every material file (textures are reloaded separately).
    void reloadAll();
    void clear();
    size_t count() const { return materials_.size(); }

private:
    bool load(Material& m);

    TextureManager* textures_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials_;
    std::unique_ptr<Material> error_;
};

} // namespace k
