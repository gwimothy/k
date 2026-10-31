#pragma once
// Virtual filesystem with ordered search paths, like Source's gameinfo.txt search paths.
// Relative paths ("cfg/autoexec.cfg") are looked up in each search path in order.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace k::fs {

struct PakEntry {
    uint64_t offset = 0, size = 0;
};

struct SearchPath {
    std::filesystem::path root;  // a directory, or a .kpk archive file
    std::string id;              // e.g. "GAME", "MOD"
    bool archive = false;
    std::map<std::string, PakEntry> entries; // archives: lowercase path -> data range
};

void addSearchPath(const std::filesystem::path& root, std::string_view id = "GAME", bool front = false);
// Mount a .kpk archive (see writePak). Returns false if it can't be read.
bool addArchive(const std::filesystem::path& file, std::string_view id = "GAME", bool front = false);
// Pack every file under `dir` (relative paths, forward slashes) into a .kpk archive.
// Paths starting with any of `excludePrefixes` are skipped.
bool writePak(const std::filesystem::path& dir, const std::filesystem::path& out,
              const std::vector<std::string>& excludePrefixes = {}, std::string* error = nullptr);
void clearSearchPaths();
const std::vector<SearchPath>& searchPaths();

// Find the first existing loose file (archives have no filesystem path; use readBinary).
// If id is non-empty, only search paths with that id are used.
std::optional<std::filesystem::path> resolve(std::string_view relative, std::string_view id = {});
bool exists(std::string_view relative, std::string_view id = {});

std::optional<std::string> readText(std::string_view relative, std::string_view id = {});
std::optional<std::vector<uint8_t>> readBinary(std::string_view relative, std::string_view id = {});

// Writes into the first search path with the given id (or the first search path if id is empty).
// Creates parent directories. Returns false on failure.
bool writeText(std::string_view relative, std::string_view contents, std::string_view id = {});
bool writeBinary(std::string_view relative, const void* data, size_t size, std::string_view id = {});

// Returns the absolute path a write would go to, or nullopt if there's no suitable search path.
std::optional<std::filesystem::path> writePath(std::string_view relative, std::string_view id = {});

// Rejects absolute paths and anything containing "..".
bool isSafeRelativePath(std::string_view relative);

} // namespace k::fs
