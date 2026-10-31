#include "platform/keys.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <unordered_map>

namespace k::keys {
namespace {

// SDL keycodes for printable ASCII are the characters themselves (lowercase letters).
// Characters that would confuse the console parser get word names, like Source.
const std::unordered_map<SDL_Keycode, std::string>& specialNames() {
    static const std::unordered_map<SDL_Keycode, std::string> m = {
        {SDLK_SPACE, "space"},       {SDLK_RETURN, "enter"},       {SDLK_ESCAPE, "escape"},
        {SDLK_TAB, "tab"},           {SDLK_BACKSPACE, "backspace"}, {SDLK_UP, "uparrow"},
        {SDLK_DOWN, "downarrow"},    {SDLK_LEFT, "leftarrow"},     {SDLK_RIGHT, "rightarrow"},
        {SDLK_LSHIFT, "shift"},      {SDLK_RSHIFT, "rshift"},      {SDLK_LCTRL, "ctrl"},
        {SDLK_RCTRL, "rctrl"},       {SDLK_LALT, "alt"},           {SDLK_RALT, "ralt"},
        {SDLK_INSERT, "ins"},        {SDLK_DELETE, "del"},         {SDLK_HOME, "home"},
        {SDLK_END, "end"},           {SDLK_PAGEUP, "pgup"},        {SDLK_PAGEDOWN, "pgdn"},
        {SDLK_SEMICOLON, "semicolon"}, {SDLK_GRAVE, "`"},          {SDLK_CAPSLOCK, "capslock"},
        {SDLK_PAUSE, "pause"},       {SDLK_KP_0, "kp_ins"},        {SDLK_KP_1, "kp_end"},
        {SDLK_KP_2, "kp_downarrow"}, {SDLK_KP_3, "kp_pgdn"},       {SDLK_KP_4, "kp_leftarrow"},
        {SDLK_KP_5, "kp_5"},         {SDLK_KP_6, "kp_rightarrow"}, {SDLK_KP_7, "kp_home"},
        {SDLK_KP_8, "kp_uparrow"},   {SDLK_KP_9, "kp_pgup"},       {SDLK_KP_ENTER, "kp_enter"},
        {SDLK_KP_PLUS, "kp_plus"},   {SDLK_KP_MINUS, "kp_minus"},  {SDLK_KP_MULTIPLY, "kp_multiply"},
        {SDLK_KP_DIVIDE, "kp_slash"}, {SDLK_KP_PERIOD, "kp_del"},
        {SDLK_F1, "f1"}, {SDLK_F2, "f2"}, {SDLK_F3, "f3"}, {SDLK_F4, "f4"}, {SDLK_F5, "f5"}, {SDLK_F6, "f6"},
        {SDLK_F7, "f7"}, {SDLK_F8, "f8"}, {SDLK_F9, "f9"}, {SDLK_F10, "f10"}, {SDLK_F11, "f11"}, {SDLK_F12, "f12"},
    };
    return m;
}

const std::vector<std::string>& allNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (char c = 'a'; c <= 'z'; ++c) n.emplace_back(1, c);
        for (char c = '0'; c <= '9'; ++c) n.emplace_back(1, c);
        for (char c : std::string("-=[]\\',./")) n.emplace_back(1, c);
        for (auto& [_, name] : specialNames()) n.push_back(name);
        for (const char* m : {"mouse1", "mouse2", "mouse3", "mouse4", "mouse5", "mwheelup", "mwheeldown"})
            n.emplace_back(m);
        std::sort(n.begin(), n.end());
        return n;
    }();
    return names;
}

} // namespace

bool isKnownName(std::string_view name) {
    auto& n = allNames();
    return std::binary_search(n.begin(), n.end(), std::string(name));
}

std::vector<std::string> completeName(std::string_view prefix) {
    std::vector<std::string> out;
    for (auto& n : allNames())
        if (n.starts_with(prefix)) out.push_back(n);
    return out;
}

std::string fromEvent(const SDL_Event& e, bool& down) {
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            if (e.key.repeat) return {};
            down = e.type == SDL_EVENT_KEY_DOWN;
            // Layout-independent: use the key at this physical position on a US layout,
            // so WASD stays WASD on AZERTY etc.
            SDL_Keycode kc = SDL_GetKeyFromScancode(e.key.scancode, SDL_KMOD_NONE, false);
            if (auto it = specialNames().find(kc); it != specialNames().end()) return it->second;
            if (kc >= 32 && kc < 127) {
                std::string s(1, static_cast<char>(kc));
                if (isKnownName(s)) return s;
            }
            return {};
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            switch (e.button.button) {
                case SDL_BUTTON_LEFT: return "mouse1";
                case SDL_BUTTON_RIGHT: return "mouse2";
                case SDL_BUTTON_MIDDLE: return "mouse3";
                case SDL_BUTTON_X1: return "mouse4";
                case SDL_BUTTON_X2: return "mouse5";
                default: return {};
            }
        }
        default: return {};
    }
}

} // namespace k::keys
