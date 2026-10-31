#pragma once
// Ray traces against the solid leaves of a compiled map (Quake's RecursiveHullCheck with a
// point hull). Used by the light compiler for shadows and by the engine for line-of-sight.
// Box traces for player movement arrive in Phase 4.

#include "shared/bspfile.h"

namespace k::bsp {

struct TraceResult {
    float fraction = 1.0f;  // 1 = reached the end
    Vec3 endPos{0};
    Vec3 normal{0};         // surface normal at the hit
    uint32_t contents = 0;  // contents of the leaf that was hit
    bool startSolid = false;
};

// Distance kept between a hit point and the surface.
constexpr float kDistEpsilon = 0.03125f;

TraceResult traceLine(const BspData& bsp, const Vec3& start, const Vec3& end,
                      uint32_t mask = CONTENTS_SOLID | CONTENTS_SKY);

} // namespace k::bsp
