#pragma once
// Named shader programs loaded from base/shaders/<file>.vert/.frag, with hot reload:
// edit a shader file while the game runs and it recompiles within a second
// (r_shader_hotreload 1). A shader that fails to compile keeps its last good version.

#include "render/shader.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace k {

class ShaderLibrary {
public:
    // Name is the file stem: "lit_generic" -> shaders/lit_generic.vert + .frag.
    // Returns nullptr if the shader has never compiled successfully.
    Shader* get(std::string_view name);

    // Recompile shaders whose files changed on disk. Cheap; call once per frame.
    void pollChanges(double now);
    // Recompile everything. Returns false if any shader failed.
    bool reloadAll();
    void clear() { shaders_.clear(); }

private:
    struct Entry {
        std::unique_ptr<Shader> shader;
        std::filesystem::file_time_type vertTime, fragTime;
        bool ok = false;
    };
    bool compile(const std::string& name, Entry& e);

    std::unordered_map<std::string, Entry> shaders_;
    double nextPoll_ = 0;
};

} // namespace k
