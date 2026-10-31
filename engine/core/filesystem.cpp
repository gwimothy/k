#include "core/filesystem.h"

#include "core/log.h"

#include <fstream>

namespace k::fs {
namespace {

std::vector<SearchPath>& paths() {
    static std::vector<SearchPath> p;
    return p;
}

} // namespace

bool isSafeRelativePath(std::string_view relative) {
    if (relative.empty()) return false;
    std::filesystem::path p{std::string(relative)};
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory()) return false;
    for (auto& part : p)
        if (part == "..") return false;
    return true;
}

void addSearchPath(const std::filesystem::path& root, std::string_view id, bool front) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(root, ec);
    SearchPath sp{ec ? root : abs.lexically_normal(), std::string(id)};
    if (front)
        paths().insert(paths().begin(), sp);
    else
        paths().push_back(sp);
}

void clearSearchPaths() { paths().clear(); }

const std::vector<SearchPath>& searchPaths() { return paths(); }

std::optional<std::filesystem::path> resolve(std::string_view relative, std::string_view id) {
    if (!isSafeRelativePath(relative)) return std::nullopt;
    for (auto& sp : paths()) {
        if (!id.empty() && sp.id != id) continue;
        auto full = sp.root / std::string(relative);
        std::error_code ec;
        if (std::filesystem::is_regular_file(full, ec)) return full;
    }
    return std::nullopt;
}

bool exists(std::string_view relative, std::string_view id) { return resolve(relative, id).has_value(); }

std::optional<std::vector<uint8_t>> readBinary(std::string_view relative, std::string_view id) {
    auto path = resolve(relative, id);
    if (!path) return std::nullopt;
    std::ifstream f(*path, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(0, std::ios::end);
    auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(size);
    if (size > 0) f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return data;
}

std::optional<std::string> readText(std::string_view relative, std::string_view id) {
    auto bin = readBinary(relative, id);
    if (!bin) return std::nullopt;
    return std::string(bin->begin(), bin->end());
}

std::optional<std::filesystem::path> writePath(std::string_view relative, std::string_view id) {
    if (!isSafeRelativePath(relative)) return std::nullopt;
    for (auto& sp : paths())
        if (id.empty() || sp.id == id) return sp.root / std::string(relative);
    return std::nullopt;
}

bool writeBinary(std::string_view relative, const void* data, size_t size, std::string_view id) {
    auto path = writePath(relative, id);
    if (!path) {
        log::warn("fs: no writable search path for '{}'", relative);
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path->parent_path(), ec);
    std::ofstream f(*path, std::ios::binary | std::ios::trunc);
    if (!f) {
        log::warn("fs: couldn't open '{}' for writing", path->string());
        return false;
    }
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

bool writeText(std::string_view relative, std::string_view contents, std::string_view id) {
    return writeBinary(relative, contents.data(), contents.size(), id);
}

} // namespace k::fs
