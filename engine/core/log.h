#pragma once
// Logging. Every message goes to all registered sinks (stdout, the in-game console, ...).

#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace k::log {

enum class Level { Debug, Info, Warn, Error };

using Sink = std::function<void(Level, std::string_view)>;

// Returns an id that can be passed to removeSink.
int addSink(Sink sink);
void removeSink(int id);

// Enables/disables the default stdout/stderr sink (on by default).
void setStdoutEnabled(bool enabled);

void write(Level level, std::string_view message);

template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace k::log
