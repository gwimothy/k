#pragma once
// Immediate-mode debug drawing (Source's debugoverlay). Callable from anywhere in the
// engine; lines last for `duration` seconds (0 = this frame only).
//
//   debugdraw::line(a, b, {1, 0, 0});
//   debugdraw::box(mins, maxs, {0, 1, 0}, 5.0f);

#include "shared/kmath.h"

#include <vector>

namespace k::debugdraw {

struct Line {
    Vec3 a, b;
    Vec3 color;
    double expires; // platformTime() when it disappears
    bool depthTest;
};

void line(const Vec3& a, const Vec3& b, const Vec3& color, float duration = 0.0f, bool depthTest = true);
void box(const Vec3& mins, const Vec3& maxs, const Vec3& color, float duration = 0.0f, bool depthTest = true);
void cross(const Vec3& center, float size, const Vec3& color, float duration = 0.0f, bool depthTest = true);
void axis(const Vec3& origin, float size, float duration = 0.0f);

// Renderer interface: lines alive at `now`, then drop expired ones.
const std::vector<Line>& lines();
void prune(double now);
void clear();

} // namespace k::debugdraw
