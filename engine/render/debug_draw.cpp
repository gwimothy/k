#include "render/debug_draw.h"

#include "core/cmdbuffer.h"
#include "core/cvar.h"
#include "core/log.h"
#include "core/time.h"

#include <algorithm>
#include <cstdlib>

namespace k::debugdraw {
namespace {

std::vector<Line>& store() {
    static std::vector<Line> l;
    return l;
}

// Frame-only lines (duration 0) must survive until drawn once; give them a tiny lifetime.
constexpr double kFrameLife = 0.0001;

} // namespace

void line(const Vec3& a, const Vec3& b, const Vec3& color, float duration, bool depthTest) {
    store().push_back({a, b, color, platformTime() + (duration > 0 ? duration : kFrameLife), depthTest});
}

void box(const Vec3& lo, const Vec3& hi, const Vec3& c, float d, bool dt) {
    Vec3 p[8];
    for (int i = 0; i < 8; ++i) p[i] = Vec3((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& e : edges) line(p[e[0]], p[e[1]], c, d, dt);
}

void cross(const Vec3& o, float s, const Vec3& c, float d, bool dt) {
    line(o - Vec3(s, 0, 0), o + Vec3(s, 0, 0), c, d, dt);
    line(o - Vec3(0, s, 0), o + Vec3(0, s, 0), c, d, dt);
    line(o - Vec3(0, 0, s), o + Vec3(0, 0, s), c, d, dt);
}

void axis(const Vec3& o, float s, float d) {
    line(o, o + Vec3(s, 0, 0), {1, 0, 0}, d, false);
    line(o, o + Vec3(0, s, 0), {0, 1, 0}, d, false);
    line(o, o + Vec3(0, 0, s), {0, 0, 1}, d, false);
}

const std::vector<Line>& lines() { return store(); }

void prune(double now) {
    std::erase_if(store(), [now](const Line& l) { return l.expires < now; });
}

void clear() { store().clear(); }

namespace {

float argf(const CmdArgs& a, int i) { return static_cast<float>(std::atof(a.arg(i).c_str())); }

ConCommand cmd_drawline(
    "debug_drawline",
    [](const CmdArgs& a) {
        if (a.argc() < 7) {
            log::info("Usage: debug_drawline x1 y1 z1 x2 y2 z2 [seconds]");
            return;
        }
        line({argf(a, 1), argf(a, 2), argf(a, 3)}, {argf(a, 4), argf(a, 5), argf(a, 6)}, {1, 1, 0},
             a.argc() > 7 ? argf(a, 7) : 10.0f);
    },
    "Draw a debug line for a while.", FCVAR_CHEAT);

ConCommand cmd_drawcross(
    "debug_drawcross",
    [](const CmdArgs& a) {
        if (a.argc() < 4) {
            log::info("Usage: debug_drawcross x y z [size] [seconds]");
            return;
        }
        cross({argf(a, 1), argf(a, 2), argf(a, 3)}, a.argc() > 4 ? argf(a, 4) : 16.0f, {0, 1, 1},
              a.argc() > 5 ? argf(a, 5) : 10.0f);
    },
    "Draw a debug cross for a while.", FCVAR_CHEAT);

ConCommand cmd_clear("debug_clear", [](const CmdArgs&) { clear(); }, "Remove all debug lines.");

} // namespace
} // namespace k::debugdraw
