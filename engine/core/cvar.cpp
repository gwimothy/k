#include "core/cvar.h"

#include "core/log.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace k {
namespace {

ConCommandBase*& headRef() {
    static ConCommandBase* head = nullptr;
    return head;
}

console::CheatGate& cheatGate() {
    static console::CheatGate gate;
    return gate;
}

std::string formatFloat(float v) {
    if (std::floor(v) == v && std::fabs(v) < 1e9f) return std::to_string(static_cast<long long>(v));
    std::string s = std::to_string(v);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

float parseFloat(std::string_view s) {
    // std::from_chars for float is fine on GCC 11+/MSVC; libc++ only recently gained it,
    // so fall back to strtof which is good enough for console input.
    std::string tmp(s);
    char* end = nullptr;
    float v = std::strtof(tmp.c_str(), &end);
    return end == tmp.c_str() ? 0.0f : v;
}

} // namespace

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// ---- CmdArgs ---------------------------------------------------------------------

const std::string& CmdArgs::arg(int i) const {
    static const std::string empty;
    return (i >= 0 && i < argc()) ? tokens_[static_cast<size_t>(i)] : empty;
}

// ---- ConCommandBase --------------------------------------------------------------

ConCommandBase::ConCommandBase(std::string name, std::string help, uint32_t flags)
    : name_(std::move(name)), help_(std::move(help)), flags_(flags) {
    if (console::find(name_)) {
        // Safe during static init: the log state is a function-local static.
        log::warn("Console name '{}' registered twice", name_);
    }
    next_ = headRef();
    headRef() = this;
}

ConCommandBase::~ConCommandBase() {
    for (ConCommandBase** p = &headRef(); *p; p = &(*p)->next_) {
        if (*p == this) {
            *p = next_;
            break;
        }
    }
}

ConCommandBase* ConCommandBase::head() { return headRef(); }

// ---- ConVar ----------------------------------------------------------------------

ConVar::ConVar(std::string name, std::string defaultValue, uint32_t flags, std::string help,
               std::optional<float> min, std::optional<float> max, ChangeCallback onChange)
    : ConCommandBase(std::move(name), std::move(help), flags),
      default_(std::move(defaultValue)),
      min_(min),
      max_(max),
      onChange_(std::move(onChange)) {
    assign(default_);
}

void ConVar::assign(std::string value) {
    float f = parseFloat(value);
    bool clamped = false;
    if (min_ && f < *min_) { f = *min_; clamped = true; }
    if (max_ && f > *max_) { f = *max_; clamped = true; }
    if (clamped) value = formatFloat(f);
    value_ = std::move(value);
    floatValue_ = f;
    intValue_ = static_cast<int>(f);
}

void ConVar::setValue(std::string_view value) {
    if (hasFlag(FCVAR_CHEAT) && !console::cheatsAllowed()) {
        log::warn("Can't change cheat cvar '{}' unless sv_cheats is 1", name());
        return;
    }
    std::string old = value_;
    assign(std::string(value));
    if (value_ == old) return;
    if (hasFlag(FCVAR_NOTIFY)) log::info("'{}' changed to {}", name(), value_);
    if (onChange_) onChange_(*this, old);
}

void ConVar::setValue(float value) { setValue(std::string_view(formatFloat(value))); }
void ConVar::setValue(int value) { setValue(std::string_view(std::to_string(value))); }

// ---- ConCommand ------------------------------------------------------------------

ConCommand::ConCommand(std::string name, Callback callback, std::string help, uint32_t flags,
                       CompletionCallback completion)
    : ConCommandBase(std::move(name), std::move(help), flags),
      callback_(std::move(callback)),
      completion_(std::move(completion)) {}

void ConCommand::dispatch(const CmdArgs& args) const {
    if (hasFlag(FCVAR_CHEAT) && !console::cheatsAllowed()) {
        log::warn("Can't use cheat command '{}' unless sv_cheats is 1", name());
        return;
    }
    if (callback_) callback_(args);
}

std::vector<std::string> ConCommand::complete(std::string_view partial) const {
    return completion_ ? completion_(partial) : std::vector<std::string>{};
}

// ---- console ---------------------------------------------------------------------

namespace console {

ConCommandBase* find(std::string_view name) {
    for (auto* c = ConCommandBase::head(); c; c = c->next())
        if (iequals(c->name(), name)) return c;
    return nullptr;
}

ConVar* findVar(std::string_view name) {
    auto* c = find(name);
    return (c && !c->isCommand()) ? static_cast<ConVar*>(c) : nullptr;
}

ConCommand* findCommand(std::string_view name) {
    auto* c = find(name);
    return (c && c->isCommand()) ? static_cast<ConCommand*>(c) : nullptr;
}

std::vector<std::string> completeName(std::string_view prefix) {
    std::vector<std::string> out;
    for (auto* c = ConCommandBase::head(); c; c = c->next()) {
        const auto& n = c->name();
        if (n.size() >= prefix.size() && iequals(std::string_view(n).substr(0, prefix.size()), prefix))
            out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void setCheatGate(CheatGate gate) { cheatGate() = std::move(gate); }

bool cheatsAllowed() {
    auto& gate = cheatGate();
    return !gate || gate();
}

} // namespace console
} // namespace k
