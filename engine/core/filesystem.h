#pragma once
// Virtual filesystem with ordered search paths, like Source's gameinfo.txt search paths.
// Relative paths ("cfg/autoexec.cfg") are looked up in each search path in order.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace k::fs {

struct SearchPath {
    std::filesystem::path root;
    std::string id; // e.g. "GAME", "MOD"
};

void addSearchPath(const std::filesystem::path& root, std::string_view id = "GAME", bool front = false);
void clearSearchPaths();
const std::vector<SearchPath>& searchPaths();

// Find the first existing file. If id is non-empty, only search paths with that id are used.
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
