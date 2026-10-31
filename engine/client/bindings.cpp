#include "client/bindings.h"

#include "core/cmdbuffer.h"
#include "core/cvar.h"
#include "core/log.h"
#include "platform/keys.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace k::bindings {
namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::map<std::string, std::string>& table() {
    static std::map<std::string, std::string> t;
    return t;
}

// NOTE: calls below are qualified (bindings::bind) on purpose: an unqualified bind() with
// std::string arguments resolves to std::bind through argument-dependent lookup.

// Keys whose '+' command is currently pressed, so we can send the matching '-' command
// even if the binding changed while held.
std::map<std::string, std::string>& held() {
    static std::map<std::string, std::string> h;
    return h;
}

std::string quote(std::string_view s) {
    std::string out = "\"";
    for (char c : s) out += (c == '"') ? '\'' : c;
    return out + "\"";
}

} // namespace

bool isValidKeyName(std::string_view key) { return keys::isKnownName(lower(key)); }

void bind(std::string_view key, std::string_view command) { table()[lower(key)] = std::string(command); }

void unbind(std::string_view key) { table().erase(lower(key)); }

void unbindAll() { table().clear(); }

const std::string* find(std::string_view key) {
    auto it = table().find(lower(key));
    return it == table().end() ? nullptr : &it->second;
}

std::vector<std::pair<std::string, std::string>> all() { return {table().begin(), table().end()}; }

void onKey(std::string_view keyIn, bool down) {
    std::string key = lower(keyIn);
    if (down) {
        auto* b = find(key);
        if (!b || b->empty()) return;
        if ((*b)[0] == '+') {
            // "+cmd key" so kbuttons can track multiple keys. Only the first statement is
            // a button; anything after ';' runs as-is.
            auto statements = cmd::splitStatements(*b);
            std::string first = statements.empty() ? *b : statements[0];
            held()[key] = first;
            cmd::append(first + " " + key);
            for (size_t i = 1; i < statements.size(); ++i) cmd::append(statements[i]);
        } else {
            cmd::append(*b);
        }
    } else {
        auto it = held().find(key);
        if (it == held().end()) return;
        std::string up = it->second;
        up[0] = '-';
        auto tokens = cmd::tokenize(up);
        cmd::append((tokens.empty() ? up : tokens[0]) + " " + key);
        held().erase(it);
    }
}

void releaseAll() {
    auto keysHeld = held();
    for (auto& [key, _] : keysHeld) onKey(key, false);
}

std::string serialize() {
    std::string out = "unbindall\n";
    for (auto& [key, command] : table()) out += "bind " + quote(key) + " " + quote(command) + "\n";
    return out;
}

namespace {

ConCommand cmd_bind(
    "bind",
    [](const CmdArgs& a) {
        if (a.argc() < 2) {
            log::info("Usage: bind <key> [command]");
            return;
        }
        if (!isValidKeyName(a.arg(1))) {
            log::warn("bind: \"{}\" isn't a valid key", a.arg(1));
            return;
        }
        if (a.argc() == 2) {
            auto* b = bindings::find(a.arg(1));
            log::info("\"{}\" = \"{}\"", a.arg(1), b ? *b : "");
            return;
        }
        std::string command;
        for (int i = 2; i < a.argc(); ++i) {
            if (i > 2) command += ' ';
            command += a.arg(i);
        }
        bindings::bind(a.arg(1), command);
    },
    "Bind a key to a command: bind <key> \"<command>\".", FCVAR_NONE,
    [](std::string_view partial) { return keys::completeName(lower(partial)); });

ConCommand cmd_unbind(
    "unbind", [](const CmdArgs& a) { bindings::unbind(a.arg(1)); }, "Remove a key binding.");

ConCommand cmd_unbindall("unbindall", [](const CmdArgs&) { bindings::unbindAll(); }, "Remove all key bindings.");

ConCommand cmd_key_listboundkeys(
    "key_listboundkeys",
    [](const CmdArgs&) {
        for (auto& [k, c] : bindings::all()) log::info("\"{}\" = \"{}\"", k, c);
    },
    "List all key bindings.");

} // namespace
} // namespace k::bindings
