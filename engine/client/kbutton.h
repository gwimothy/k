#pragma once
// A held input button such as +forward (Source's kbutton_t).
// Up to two keys can hold the same button; it is released when both are up.

#include <string>

namespace k {

struct KButton {
    std::string keys[2];
    bool down = false;
    bool pressedThisTick = false; // set on press, cleared after each usercmd

    void press(const std::string& key);
    void release(const std::string& key);
    // 1 if held, 0 otherwise. (Kept as float for analog input later.)
    float state() const { return down ? 1.0f : 0.0f; }
};

} // namespace k
