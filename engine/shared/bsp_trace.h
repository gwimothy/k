#pragma once
// Traces against a compiled map.
//
//   traceLine: a ray against solid leaves (Quake's RecursiveHullCheck). Used by the light
//              compiler and for line-of-sight.
//   traceBox:  an axis-aligned box swept against the map's brushes (Quake 3's CM_Trace).
//              Used for player movement and anything else with a size.

#include "shared/bspfile.h"

namespace k::bsp {

struct TraceResult {
    float fraction = 1.0f;  // 1 = reached the end
    Vec3 endPos{0};
    Vec3 normal{0};         // surface normal at the hit
    uint32_t contents = 0;  // contents of the leaf that was hit
    bool startSolid = false;
    bool allSolid = false;  // the whole move was inside solid (box traces only)
    int entity = -1;        // what was hit: 0 = world, > 0 an entity (set by the game), -1 nothing
};

// Distance kept between a hit point and the surface.
constexpr float kDistEpsilon = 0.03125f;

TraceResult traceLine(const BspData& bsp, const Vec3& start, const Vec3& end,
                      uint32_t mask = CONTENTS_SOLID | CONTENTS_SKY);

// mins/maxs are relative to start/end, e.g. a player hull (-16 -16 0) (16 16 72).
TraceResult traceBox(const BspData& bsp, const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                     uint32_t mask = CONTENTS_SOLID | CONTENTS_SKY | CONTENTS_PLAYERCLIP);

// Box trace against one brush entity model whose origin has moved by `offset` from where it
// was compiled (doors, elevators). Tests the model's brushes directly.
TraceResult traceBoxModel(const BspData& bsp, int model, const Vec3& offset, const Vec3& start, const Vec3& end,
                          const Vec3& mins, const Vec3& maxs, uint32_t mask = CONTENTS_SOLID | CONTENTS_PLAYERCLIP);

} // namespace k::bsp
