#include "core/filesystem.h"

#include "core/log.h"

#include <algorithm>
#include <cctype>
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
        if (sp.archive) continue;
        auto full = sp.root / std::string(relative);
        std::error_code ec;
        if (std::filesystem::is_regular_file(full, ec)) return full;
    }
    return std::nullopt;
}

namespace {

std::string pakKey(std::string_view relative) {
    std::string k(relative);
    std::replace(k.begin(), k.end(), '\\', '/');
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::tolower(c); });
    return k;
}

constexpr uint32_t kPakMagic = 0x4B41504B; // "KPAK"
constexpr uint32_t kPakVersion = 1;

} // namespace

bool addArchive(const std::filesystem::path& file, std::string_view id, bool front) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    auto read32 = [&](uint32_t& v) { return static_cast<bool>(f.read(reinterpret_cast<char*>(&v), 4)); };
    uint32_t magic = 0, version = 0, count = 0;
    if (!read32(magic) || magic != kPakMagic || !read32(version) || version != kPakVersion || !read32(count)) {
        log::warn("fs: '{}' isn't a .kpk archive", file.string());
        return false;
    }
    f.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(f.tellg());
    f.seekg(12);
    SearchPath sp;
    sp.root = file;
    sp.id = std::string(id);
    sp.archive = true;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t len = 0;
        if (!f.read(reinterpret_cast<char*>(&len), 2)) return false;
        std::string path(len, '\0');
        PakEntry e;
        if (!f.read(path.data(), len) || !f.read(reinterpret_cast<char*>(&e.offset), 8) ||
            !f.read(reinterpret_cast<char*>(&e.size), 8))
            return false;
        if (e.offset + e.size > fileSize || !isSafeRelativePath(path)) {
            log::warn("fs: '{}' has a bad entry", file.string());
            return false;
        }
        sp.entries[pakKey(path)] = e;
    }
    if (front) paths().insert(paths().begin(), std::move(sp));
    else paths().push_back(std::move(sp));
    return true;
}

bool writePak(const std::filesystem::path& dir, const std::filesystem::path& out,
              const std::vector<std::string>& excludePrefixes, std::string* error) {
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    std::error_code ec;
    for (auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string rel = std::filesystem::relative(entry.path(), dir).generic_string();
        bool skip = false;
        for (auto& p : excludePrefixes) skip |= rel.rfind(p, 0) == 0;
        if (!skip) files.emplace_back(rel, entry.path());
    }
    if (ec) {
        if (error) *error = "can't read " + dir.string();
        return false;
    }
    std::sort(files.begin(), files.end());
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = "can't write " + out.string();
        return false;
    }
    auto write32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    write32(kPakMagic);
    write32(kPakVersion);
    write32(static_cast<uint32_t>(files.size()));
    uint64_t offset = 12;
    for (auto& [rel, _] : files) offset += 2 + rel.size() + 16;
    for (auto& [rel, path] : files) {
        uint16_t len = static_cast<uint16_t>(rel.size());
        uint64_t size = std::filesystem::file_size(path, ec);
        f.write(reinterpret_cast<const char*>(&len), 2);
        f.write(rel.data(), len);
        f.write(reinterpret_cast<const char*>(&offset), 8);
        f.write(reinterpret_cast<const char*>(&size), 8);
        offset += size;
    }
    for (auto& [rel, path] : files) {
        std::ifstream in(path, std::ios::binary);
        f << in.rdbuf();
    }
    return static_cast<bool>(f);
}

bool exists(std::string_view relative, std::string_view id) {
    if (!isSafeRelativePath(relative)) return false;
    std::string key = pakKey(relative);
    for (auto& sp : paths()) {
        if (!id.empty() && sp.id != id) continue;
        if (sp.archive) {
            if (sp.entries.count(key)) return true;
        } else {
            std::error_code ec;
            if (std::filesystem::is_regular_file(sp.root / std::string(relative), ec)) return true;
        }
    }
    return false;
}

std::optional<std::vector<uint8_t>> readBinary(std::string_view relative, std::string_view id) {
    if (!isSafeRelativePath(relative)) return std::nullopt;
    // Search paths in order; loose directories and archives can be mixed.
    std::string key = pakKey(relative);
    std::optional<std::filesystem::path> path;
    for (auto& sp : paths()) {
        if (!id.empty() && sp.id != id) continue;
        if (sp.archive) {
            auto it = sp.entries.find(key);
            if (it == sp.entries.end()) continue;
            std::ifstream f(sp.root, std::ios::binary);
            std::vector<uint8_t> data(static_cast<size_t>(it->second.size));
            f.seekg(static_cast<std::streamoff>(it->second.offset));
            if (!data.empty()) f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!f) return std::nullopt;
            return data;
        }
        auto full = sp.root / std::string(relative);
        std::error_code ec;
        if (std::filesystem::is_regular_file(full, ec)) {
            path = full;
            break;
        }
    }
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
        if (!sp.archive && (id.empty() || sp.id == id)) return sp.root / std::string(relative);
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
