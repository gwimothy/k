#pragma once
// The in-game developer console (toggle with ` by default), drawn with Dear ImGui.

#include "core/log.h"

#include <deque>
#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace k {

class ConsoleUI {
public:
    ConsoleUI();
    ~ConsoleUI();
    ConsoleUI(const ConsoleUI&) = delete;
    ConsoleUI& operator=(const ConsoleUI&) = delete;

    void draw();
    bool open() const { return open_; }
    void setOpen(bool open);
    void toggle() { setOpen(!open_); }

private:
    struct Line {
        log::Level level;
        std::string text;
    };

    void submit();
    static int inputCallback(ImGuiInputTextCallbackData* data);

    int sinkId_ = 0;
    std::deque<Line> lines_;
    std::vector<std::string> history_;
    int historyPos_ = -1;
    char input_[512] = {};
    bool open_ = false;
    bool focusInput_ = false;
    bool scrollToBottom_ = false;
};

} // namespace k
