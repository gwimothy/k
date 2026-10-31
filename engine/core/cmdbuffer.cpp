#include "core/cmdbuffer.h"

#include "core/cvar.h"
#include "core/filesystem.h"
#include "core/log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <map>

namespace k::cmd {
namespace {

struct LowerLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
        });
    }
};

struct State {
    std::deque<std::string> queue;
    std::map<std::string, std::string, LowerLess> aliases;
    int waitFrames = 0;
};

State& state() {
    static State s;
    return s;
}

// Guards against runaway alias recursion like: alias a "a"
constexpr int kMaxStatementsPerFrame = 8192;

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

void dispatch(const std::string& statement);

} // namespace

std::vector<std::string> splitStatements(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    bool inQuotes = false;
    auto flush = [&] {
        auto t = trim(current);
        if (!t.empty()) out.push_back(std::move(t));
        current.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '"') {
            inQuotes = !inQuotes;
            current += c;
        } else if (!inQuotes && c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            // Comment: skip to end of line.
            while (i < text.size() && text[i] != '\n') ++i;
            flush();
        } else if (c == '\n' || c == '\r') {
            inQuotes = false; // quotes never span lines
            flush();
        } else if (c == ';' && !inQuotes) {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return out;
}

std::vector<std::string> tokenize(std::string_view statement) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < statement.size()) {
        while (i < statement.size() && std::isspace(static_cast<unsigned char>(statement[i]))) ++i;
        if (i >= statement.size()) break;
        std::string tok;
        if (statement[i] == '"') {
            ++i;
            while (i < statement.size() && statement[i] != '"') tok += statement[i++];
            if (i < statement.size()) ++i; // closing quote
        } else {
            while (i < statement.size() && !std::isspace(static_cast<unsigned char>(statement[i])))
                tok += statement[i++];
        }
        tokens.push_back(std::move(tok));
    }
    return tokens;
}

void append(std::string_view text) {
    for (auto& s : splitStatements(text)) state().queue.push_back(std::move(s));
}

void insert(std::string_view text) {
    auto statements = splitStatements(text);
    auto& q = state().queue;
    for (auto it = statements.rbegin(); it != statements.rend(); ++it) q.push_front(std::move(*it));
}

void execute() {
    auto& s = state();
    if (s.waitFrames > 0) {
        --s.waitFrames;
        return;
    }
    int count = 0;
    while (!s.queue.empty()) {
        if (++count > kMaxStatementsPerFrame) {
            log::error("Command buffer overflow (alias loop?), clearing {} statements", s.queue.size());
            s.queue.clear();
            break;
        }
        std::string statement = std::move(s.queue.front());
        s.queue.pop_front();
        dispatch(statement);
        if (s.waitFrames > 0) break;
    }
}

void executeNow(std::string_view text) {
    for (auto& s : splitStatements(text)) dispatch(s);
}

void clear() {
    state().queue.clear();
    state().waitFrames = 0;
}

bool empty() { return state().queue.empty(); }

void setAlias(std::string_view name, std::string_view commands) {
    state().aliases[std::string(name)] = std::string(commands);
}

const std::string* findAlias(std::string_view name) {
    auto& a = state().aliases;
    auto it = a.find(std::string(name));
    return it == a.end() ? nullptr : &it->second;
}

std::vector<std::pair<std::string, std::string>> aliases() {
    return {state().aliases.begin(), state().aliases.end()};
}

namespace {

void dispatch(const std::string& statement) {
    auto tokens = tokenize(statement);
    if (tokens.empty()) return;

    // Raw argument string: everything after the first token.
    std::string argString;
    {
        size_t i = 0;
        while (i < statement.size() && std::isspace(static_cast<unsigned char>(statement[i]))) ++i;
        if (i < statement.size() && statement[i] == '"') {
            i = statement.find('"', i + 1);
            i = (i == std::string::npos) ? statement.size() : i + 1;
        } else {
            while (i < statement.size() && !std::isspace(static_cast<unsigned char>(statement[i]))) ++i;
        }
        argString = trim(std::string_view(statement).substr(std::min(i, statement.size())));
    }

    const std::string& name = tokens[0];
    if (auto* base = console::find(name)) {
        if (base->isCommand()) {
            static_cast<ConCommand*>(base)->dispatch(CmdArgs(std::move(tokens), std::move(argString)));
        } else {
            auto* var = static_cast<ConVar*>(base);
            if (tokens.size() == 1) {
                log::info("\"{}\" = \"{}\" (def. \"{}\"){}{}", var->name(), var->getString(), var->defaultValue(),
                          var->help().empty() ? "" : " - ", var->help());
            } else {
                var->setValue(std::string_view(tokens[1]));
            }
        }
        return;
    }
    if (auto* alias = findAlias(name)) {
        insert(*alias);
        return;
    }
    log::warn("Unknown command \"{}\"", name);
}

// ---- Built-in commands -------------------------------------------------------------

ConCommand cmd_echo(
    "echo",
    [](const CmdArgs& a) {
        std::string out;
        for (int i = 1; i < a.argc(); ++i) out += (i > 1 ? " " : "") + a.arg(i);
        log::info("{}", out);
    },
    "Print text to the console.");

ConCommand cmd_wait(
    "wait",
    [](const CmdArgs& a) {
        int frames = a.argc() > 1 ? std::max(1, std::atoi(a.arg(1).c_str())) : 1;
        state().waitFrames = frames;
    },
    "Pause the command buffer for N frames (default 1).");

ConCommand cmd_alias(
    "alias",
    [](const CmdArgs& a) {
        if (a.argc() == 1) {
            for (auto& [n, v] : aliases()) log::info("{} : {}", n, v);
            return;
        }
        if (console::find(a.arg(1))) {
            log::warn("alias: '{}' is already a command or cvar", a.arg(1));
            return;
        }
        // Everything after the alias name, so `alias x echo hi; echo there` style isn't needed:
        // the quoted form `alias x "echo hi; echo there"` is the normal usage.
        std::string body;
        for (int i = 2; i < a.argc(); ++i) {
            if (i > 2) body += ' ';
            body += a.arg(i);
        }
        setAlias(a.arg(1), body);
    },
    "Create a command alias: alias <name> \"<commands>\".");

ConCommand cmd_exec(
    "exec",
    [](const CmdArgs& a) {
        if (a.argc() < 2) {
            log::info("Usage: exec <file>");
            return;
        }
        std::string path = a.arg(1);
        if (path.find('.') == std::string::npos) path += ".cfg";
        auto text = fs::readText("cfg/" + path);
        if (!text) {
            log::warn("Couldn't exec {}", path);
            return;
        }
        log::debug("Executing {}", path);
        insert(*text);
    },
    "Run a config file from cfg/.");

ConCommand cmd_toggle(
    "toggle",
    [](const CmdArgs& a) {
        auto* var = console::findVar(a.arg(1));
        if (!var) {
            log::warn("toggle: unknown cvar '{}'", a.arg(1));
            return;
        }
        if (a.argc() <= 2) {
            var->setValue(var->getBool() ? 0 : 1);
            return;
        }
        // Cycle through the given values.
        int next = 2;
        for (int i = 2; i < a.argc(); ++i) {
            if (a.arg(i) == var->getString()) {
                next = (i + 1 < a.argc()) ? i + 1 : 2;
                break;
            }
        }
        var->setValue(std::string_view(a.arg(next)));
    },
    "Toggle a cvar between 0/1, or cycle: toggle <cvar> [values...].");

ConCommand cmd_incrementvar(
    "incrementvar",
    [](const CmdArgs& a) {
        auto* var = console::findVar(a.arg(1));
        if (!var || a.argc() < 5) {
            log::info("Usage: incrementvar <cvar> <min> <max> <delta>");
            return;
        }
        float mn = std::stof(a.arg(2)), mx = std::stof(a.arg(3)), d = std::stof(a.arg(4));
        float v = var->getFloat() + d;
        if (v > mx) v = mn;
        if (v < mn) v = mx;
        var->setValue(v);
    },
    "incrementvar <cvar> <min> <max> <delta>: step a cvar, wrapping around.");

ConCommand cmd_find(
    "find",
    [](const CmdArgs& a) {
        std::string needle = a.arg(1);
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return std::tolower(c); });
        console::forEach([&](ConCommandBase& c) {
            std::string hay = c.name() + " " + c.help();
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char ch) { return std::tolower(ch); });
            if (hay.find(needle) != std::string::npos) log::info("{} - {}", c.name(), c.help());
        });
    },
    "Find commands and cvars whose name or help contains a string.");

ConCommand cmd_help(
    "help",
    [](const CmdArgs& a) {
        auto* c = console::find(a.arg(1));
        if (!c) {
            log::info("help: no cvar or command named '{}'", a.arg(1));
            return;
        }
        log::info("{} - {}", c->name(), c->help().empty() ? "(no help)" : c->help());
    },
    "Show help text for a command or cvar.");

ConCommand cmd_cvarlist(
    "cvarlist",
    [](const CmdArgs& a) {
        auto names = console::completeName(a.arg(1));
        for (auto& n : names) {
            auto* c = console::find(n);
            if (c->isCommand())
                log::info("{:<28} cmd   {}", n, c->help());
            else
                log::info("{:<28} {:<5} {}", n, static_cast<ConVar*>(c)->getString(), c->help());
        }
        log::info("{} total", names.size());
    },
    "List all commands and cvars (optionally only those starting with a prefix).");

} // namespace
} // namespace k::cmd
