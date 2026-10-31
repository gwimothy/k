#include "render/model.h"

#include "core/filesystem.h"
#include "core/log.h"
#include "render/material.h"

namespace k {

Model* ModelManager::find(std::string_view path) {
    std::string key(path);
    if (auto it = models_.find(key); it != models_.end()) return it->second.get();

    std::unique_ptr<Model> model;
    auto bytes = fs::readBinary(path);
    if (!bytes) {
        log::warn("Missing model '{}'", path);
    } else {
        std::string error;
        auto data = loadGltf(*bytes, path, &error);
        if (!data) {
            log::warn("Couldn't load model '{}': {}", path, error);
        } else {
            model = std::make_unique<Model>();
            model->name = key;
            model->mesh = std::make_unique<Mesh>();
            model->mesh->upload(*data, *materials_);
        }
    }
    auto* ptr = model.get();
    models_.emplace(std::move(key), std::move(model)); // cache failures too
    return ptr;
}

} // namespace k
