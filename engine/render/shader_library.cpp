#include "render/shader_library.h"

#include "core/cvar.h"
#include "core/filesystem.h"
#include "core/log.h"

namespace k {
namespace {

ConVar r_shader_hotreload("r_shader_hotreload", "1", FCVAR_ARCHIVE, "Recompile shaders automatically when their files change.");

std::filesystem::file_time_type mtime(const std::string& relative) {
    auto p = fs::resolve(relative);
    std::error_code ec;
    return p ? std::filesystem::last_write_time(*p, ec) : std::filesystem::file_time_type{};
}

} // namespace

bool ShaderLibrary::compile(const std::string& name, Entry& e) {
    e.vertTime = mtime("shaders/" + name + ".vert");
    e.fragTime = mtime("shaders/" + name + ".frag");
    auto s = std::make_unique<Shader>();
    if (!s->load(name)) return false; // keep the previous version if there is one
    e.shader = std::move(s);
    e.ok = true;
    return true;
}

Shader* ShaderLibrary::get(std::string_view name) {
    std::string key(name);
    auto it = shaders_.find(key);
    if (it == shaders_.end()) {
        it = shaders_.emplace(key, Entry{}).first;
        compile(key, it->second);
    }
    return it->second.ok ? it->second.shader.get() : nullptr;
}

void ShaderLibrary::pollChanges(double now) {
    if (!r_shader_hotreload.getBool() || now < nextPoll_) return;
    nextPoll_ = now + 0.5;
    for (auto& [name, e] : shaders_) {
        if (mtime("shaders/" + name + ".vert") != e.vertTime || mtime("shaders/" + name + ".frag") != e.fragTime) {
            if (compile(name, e)) log::info("Reloaded shader '{}'", name);
        }
    }
}

bool ShaderLibrary::reloadAll() {
    bool ok = true;
    for (auto& [name, e] : shaders_) ok &= compile(name, e);
    return ok;
}

} // namespace k
