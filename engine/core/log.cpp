#include "core/log.h"

#include <cstdio>
#include <mutex>
#include <vector>

namespace k::log {
namespace {

struct SinkEntry {
    int id;
    Sink sink;
};

struct State {
    std::mutex mutex;
    std::vector<SinkEntry> sinks;
    int nextId = 1;
    bool stdoutEnabled = true;
};

State& state() {
    static State s;
    return s;
}

const char* prefix(Level level) {
    switch (level) {
        case Level::Debug: return "[debug] ";
        case Level::Warn: return "[warn] ";
        case Level::Error: return "[error] ";
        default: return "";
    }
}

} // namespace

int addSink(Sink sink) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.sinks.push_back({s.nextId, std::move(sink)});
    return s.nextId++;
}

void removeSink(int id) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    std::erase_if(s.sinks, [id](const SinkEntry& e) { return e.id == id; });
}

void setStdoutEnabled(bool enabled) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.stdoutEnabled = enabled;
}

void write(Level level, std::string_view message) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (s.stdoutEnabled) {
        FILE* out = level >= Level::Warn ? stderr : stdout;
        std::fprintf(out, "%s%.*s\n", prefix(level), static_cast<int>(message.size()), message.data());
        std::fflush(out);
    }
    for (auto& e : s.sinks) e.sink(level, message);
}

} // namespace k::log
