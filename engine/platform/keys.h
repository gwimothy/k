#pragma once
// Engine key names used by bindings ("w", "space", "mouse1", "mwheelup", "f5", ...).

#include <string>
#include <string_view>
#include <vector>

union SDL_Event;

namespace k::keys {

bool isKnownName(std::string_view name);
std::vector<std::string> completeName(std::string_view prefix);

// Map an SDL key/mouse event to an engine key name. Returns empty if unmapped.
// `down` is set to whether this is a press or release.
std::string fromEvent(const SDL_Event& event, bool& down);

} // namespace k::keys
