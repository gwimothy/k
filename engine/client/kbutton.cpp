#include "client/kbutton.h"

namespace k {

void KButton::press(const std::string& key) {
    // Typed in the console with no key: hold until released the same way.
    std::string k = key.empty() ? "<console>" : key;
    if (keys[0] == k || keys[1] == k) return;
    if (keys[0].empty())
        keys[0] = k;
    else if (keys[1].empty())
        keys[1] = k;
    else
        return; // three keys on one button: ignore
    if (!down) pressedThisTick = true;
    down = true;
}

void KButton::release(const std::string& key) {
    if (key.empty()) {
        // "-forward" from the console with no key releases everything.
        keys[0].clear();
        keys[1].clear();
    } else {
        if (keys[0] == key) keys[0].clear();
        if (keys[1] == key) keys[1].clear();
    }
    down = !keys[0].empty() || !keys[1].empty();
}

} // namespace k
