#pragma once
// Console variables and console commands, modelled after Source's ConVar / ConCommand.
//
// Declare them at file scope and they register themselves:
//
//   static k::ConVar sv_gravity("sv_gravity", "800", k::FCVAR_REPLICATED, "World gravity.");
//   static k::ConCommand quit("quit", [](const k::CmdArgs&) { ... }, "Exit the engine.");

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace k {

enum CvarFlags : uint32_t {
    FCVAR_NONE = 0,
    FCVAR_ARCHIVE = 1u << 0,     // saved to config.cfg
    FCVAR_CHEAT = 1u << 1,       // only changeable when sv_cheats is 1
    FCVAR_REPLICATED = 1u << 2,  // server value is sent to clients
    FCVAR_NOTIFY = 1u << 3,      // announce changes
    FCVAR_DEVELOPMENT = 1u << 4, // hidden from completion in release builds
};

// Arguments passed to a command. arg(0) is the command name.
class CmdArgs {
public:
    CmdArgs() = default;
    CmdArgs(std::vector<std::string> tokens, std::string argString)
        : tokens_(std::move(tokens)), argString_(std::move(argString)) {}

    int argc() const { return static_cast<int>(tokens_.size()); }
    // Out-of-range indices return an empty string.
    const std::string& arg(int i) const;
    // Everything after the command name, as typed (quotes included).
    const std::string& argString() const { return argString_; }
    const std::vector<std::string>& tokens() const { return tokens_; }

private:
    std::vector<std::string> tokens_;
    std::string argString_;
};

class ConCommandBase {
public:
    ConCommandBase(const ConCommandBase&) = delete;
    ConCommandBase& operator=(const ConCommandBase&) = delete;
    virtual ~ConCommandBase();

    const std::string& name() const { return name_; }
    const std::string& help() const { return help_; }
    uint32_t flags() const { return flags_; }
    bool hasFlag(uint32_t f) const { return (flags_ & f) != 0; }
    virtual bool isCommand() const = 0;

    ConCommandBase* next() const { return next_; }
    static ConCommandBase* head();

protected:
    ConCommandBase(std::string name, std::string help, uint32_t flags);

private:
    std::string name_;
    std::string help_;
    uint32_t flags_;
    ConCommandBase* next_ = nullptr;
};

class ConVar final : public ConCommandBase {
public:
    using ChangeCallback = std::function<void(ConVar& var, const std::string& oldValue)>;

    ConVar(std::string name, std::string defaultValue, uint32_t flags = FCVAR_NONE, std::string help = {},
           std::optional<float> min = std::nullopt, std::optional<float> max = std::nullopt,
           ChangeCallback onChange = nullptr);

    bool isCommand() const override { return false; }

    const std::string& getString() const { return value_; }
    float getFloat() const { return floatValue_; }
    int getInt() const { return intValue_; }
    bool getBool() const { return intValue_ != 0; }
    const std::string& defaultValue() const { return default_; }

    // Values are clamped to [min, max] if set. Fires the change callback when the value changes.
    void setValue(std::string_view value);
    void setValue(float value);
    void setValue(int value);
    void revert() { setValue(std::string_view(default_)); }
    void setChangeCallback(ChangeCallback cb) { onChange_ = std::move(cb); }

    std::optional<float> min() const { return min_; }
    std::optional<float> max() const { return max_; }

private:
    void assign(std::string value);

    std::string default_;
    std::string value_;
    float floatValue_ = 0.0f;
    int intValue_ = 0;
    std::optional<float> min_, max_;
    ChangeCallback onChange_;
};

class ConCommand final : public ConCommandBase {
public:
    using Callback = std::function<void(const CmdArgs&)>;
    using CompletionCallback = std::function<std::vector<std::string>(std::string_view partial)>;

    ConCommand(std::string name, Callback callback, std::string help = {}, uint32_t flags = FCVAR_NONE,
               CompletionCallback completion = nullptr);

    bool isCommand() const override { return true; }
    void dispatch(const CmdArgs& args) const;
    std::vector<std::string> complete(std::string_view partial) const;

private:
    Callback callback_;
    CompletionCallback completion_;
};

namespace console {

// Case-insensitive lookups across all registered vars/commands.
ConCommandBase* find(std::string_view name);
ConVar* findVar(std::string_view name);
ConCommand* findCommand(std::string_view name);

// Names starting with prefix (case-insensitive), sorted.
std::vector<std::string> completeName(std::string_view prefix);

template <class Fn>
void forEach(Fn&& fn) {
    for (auto* c = ConCommandBase::head(); c; c = c->next()) fn(*c);
}

// "Can this cvar be changed right now?" hook, used to enforce FCVAR_CHEAT.
// If it returns false the change is refused and a message is printed.
using CheatGate = std::function<bool()>;
void setCheatGate(CheatGate gate);
bool cheatsAllowed();

} // namespace console

bool iequals(std::string_view a, std::string_view b);

} // namespace k
