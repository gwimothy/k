#pragma once
// Models loaded from glTF 2.0 (.gltf / .glb). Each glTF material name is looked up in the
// material system, so a glTF material called "models/props/crate" uses
// materials/models/props/crate.kmt (or .png).
//
// Units: glTF files are expected in engine units (inches), Z up. Exporters that write
// Y-up meters can be converted with `$scale`/`$upaxis` later (Phase 5 model compiler).

#include "render/mesh.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace k {

class MaterialSystem;

struct Model {
    std::string name;
    std::unique_ptr<Mesh> mesh;
};

// Parse a glTF/GLB file into MeshData (exposed for tests and tools).
std::optional<MeshData> loadGltf(const std::vector<uint8_t>& fileData, std::string_view debugName, std::string* error);

class ModelManager {
public:
    void init(MaterialSystem* materials) { materials_ = materials; }
    // Path relative to the game dir, e.g. "models/props/crate.glb". Returns null if it can't load.
    Model* find(std::string_view path);
    void clear() { models_.clear(); }
    size_t count() const { return models_.size(); }

private:
    MaterialSystem* materials_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Model>> models_;
};

} // namespace k
