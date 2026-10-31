#pragma once
#include <chrono>
#include <cstdint>

namespace k {

// Seconds since the first call, as a double (plenty of precision for years of uptime).
inline double platformTime() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

} // namespace k
