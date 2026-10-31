#include "ui/console_ui.h"

#include "core/cmdbuffer.h"
#include "core/cvar.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace k {
namespace {
constexpr size_t kMaxLines = 2000;
}

ConsoleUI::ConsoleUI() {
    sinkId_ = log::addSink([this](log::Level level, std::string_view text) {
        // Multi-line messages become multiple lines.
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string_view::npos) end = text.size();
            lines_.push_back({level, std::string(text.substr(start, end - start))});
            start = end + 1;
        }
        while (lines_.size() > kMaxLines) lines_.pop_front();
        scrollToBottom_ = true;
    });
}

ConsoleUI::~ConsoleUI() { log::removeSink(sinkId_); }

void ConsoleUI::setOpen(bool open) {
    open_ = open;
    if (open) focusInput_ = true;
}

void ConsoleUI::draw() {
    if (!open_) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.1f, io.DisplaySize.y * 0.08f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.8f, io.DisplaySize.y * 0.6f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (!ImGui::Begin("Console", &open_)) {
        ImGui::End();
        return;
    }

    float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("scroll", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (auto& line : lines_) {
            ImVec4 color;
            switch (line.level) {
                case log::Level::Debug: color = ImVec4(0.6f, 0.6f, 0.6f, 1); break;
                case log::Level::Warn: color = ImVec4(1.0f, 0.8f, 0.3f, 1); break;
                case log::Level::Error: color = ImVec4(1.0f, 0.4f, 0.4f, 1); break;
                default: color = ImVec4(0.9f, 0.9f, 0.9f, 1); break;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopStyleColor();
        }
        if (scrollToBottom_ || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        scrollToBottom_ = false;
    }
    ImGui::EndChild();
    ImGui::Separator();

    ImGui::PushItemWidth(-1);
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
                                ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("##input", input_, sizeof(input_), flags, &ConsoleUI::inputCallback, this)) {
        submit();
        focusInput_ = true;
    }
    ImGui::PopItemWidth();
    ImGui::SetItemDefaultFocus();
    if (focusInput_) {
        ImGui::SetKeyboardFocusHere(-1);
        focusInput_ = false;
    }

    // Suggestions for what's being typed.
    if (input_[0] && !std::strchr(input_, ' ')) {
        auto matches = console::completeName(input_);
        if (!matches.empty()) {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
            ImGui::SetNextWindowBgAlpha(0.95f);
            ImGui::BeginTooltip();
            for (size_t i = 0; i < std::min<size_t>(matches.size(), 12); ++i) {
                auto* var = console::findVar(matches[i]);
                if (var)
                    ImGui::Text("%s  %s", matches[i].c_str(), var->getString().c_str());
                else
                    ImGui::TextUnformatted(matches[i].c_str());
            }
            if (matches.size() > 12) ImGui::TextDisabled("... %zu more", matches.size() - 12);
            ImGui::EndTooltip();
        }
    }
    ImGui::End();
}

void ConsoleUI::submit() {
    std::string text = input_;
    input_[0] = 0;
    if (text.find_first_not_of(" \t") == std::string::npos) return;
    log::info("] {}", text);
    std::erase(history_, text);
    history_.push_back(text);
    historyPos_ = -1;
    cmd::append(text);
}

int ConsoleUI::inputCallback(ImGuiInputTextCallbackData* data) {
    auto* self = static_cast<ConsoleUI*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        std::string word(data->Buf, static_cast<size_t>(data->BufTextLen));
        if (word.find(' ') != std::string::npos) return 0;
        auto matches = console::completeName(word);
        if (matches.empty()) return 0;
        // Complete to the longest common prefix; with one match, add a trailing space.
        std::string prefix = matches[0];
        for (auto& m : matches) {
            size_t i = 0;
            while (i < prefix.size() && i < m.size() && std::tolower(prefix[i]) == std::tolower(m[i])) ++i;
            prefix.resize(i);
        }
        if (matches.size() == 1) prefix += ' ';
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, prefix.c_str());
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        auto& h = self->history_;
        if (h.empty()) return 0;
        int prev = self->historyPos_;
        if (data->EventKey == ImGuiKey_UpArrow) {
            self->historyPos_ = self->historyPos_ == -1 ? static_cast<int>(h.size()) - 1 : std::max(0, self->historyPos_ - 1);
        } else if (data->EventKey == ImGuiKey_DownArrow && self->historyPos_ != -1) {
            if (++self->historyPos_ >= static_cast<int>(h.size())) self->historyPos_ = -1;
        }
        if (prev != self->historyPos_) {
            data->DeleteChars(0, data->BufTextLen);
            if (self->historyPos_ >= 0) data->InsertChars(0, h[static_cast<size_t>(self->historyPos_)].c_str());
        }
    }
    return 0;
}

} // namespace k
