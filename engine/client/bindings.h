#pragma once
// Key bindings: `bind w +forward`, `bind mouse1 +attack`, `bind f5 screenshot`.
//
// Bindings starting with '+' run "+cmd <key>" on press and "-cmd <key>" on release, so a
// button stays held while any key bound to it is down.

#include <string>
#include <string_view>
#include <vector>

namespace k::bindings {

void bind(std::string_view key, std::string_view command);
void unbind(std::string_view key);
void unbindAll();
const std::string* find(std::string_view key);
std::vector<std::pair<std::string, std::string>> all();

// Called by the input system. Key names are lowercase ("w", "space", "mouse1", ...).
void onKey(std::string_view key, bool down);

// Release every held '+' binding (e.g. when the console opens or the window loses focus).
void releaseAll();

// Text for config.cfg: "unbindall" followed by one bind line per key.
std::string serialize();

bool isValidKeyName(std::string_view key);

} // namespace k::bindings
