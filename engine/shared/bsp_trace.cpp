#include "shared/bsp_trace.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace k::bsp {
namespace {

struct TraceWork {
    const BspData* bsp;
    uint32_t mask;
    Vec3 start, end;
    TraceResult result;
    bool hit = false;
    bool positioned = false; // fraction/normal have been set by the split that entered solid
};

// Returns true if the segment [p1f, p2f] hits solid. When it does and the hit was entering
// a solid leaf directly from this call's split, the split sets the fraction.
bool recurse(TraceWork& w, int node, float p1f, float p2f, const Vec3& p1, const Vec3& p2) {
    if (node < 0) {
        const Leaf& leaf = w.bsp->leaves[static_cast<size_t>(-1 - node)];
        if (leaf.contents & w.mask) {
            w.result.contents = leaf.contents;
            return true;
        }
        return false;
    }
    const Node& n = w.bsp->nodes[static_cast<size_t>(node)];
    const Plane& plane = w.bsp->planes[static_cast<size_t>(n.plane)];
    float d1 = plane.distance(p1), d2 = plane.distance(p2);
    if (d1 >= 0 && d2 >= 0) return recurse(w, n.children[0], p1f, p2f, p1, p2);
    if (d1 < 0 && d2 < 0) return recurse(w, n.children[1], p1f, p2f, p1, p2);

    int side = d1 < 0 ? 1 : 0;
    float frac = std::clamp(d1 / (d1 - d2), 0.0f, 1.0f);
    float midf = p1f + (p2f - p1f) * frac;
    Vec3 mid = p1 + (p2 - p1) * frac;

    if (recurse(w, n.children[side], p1f, midf, p1, mid)) return true;
    if (!recurse(w, n.children[side ^ 1], midf, p2f, mid, p2)) return false;
    if (!w.positioned) {
        // We crossed from empty (this side) into solid (other side) at this plane.
        w.positioned = true;
        float len = glm::length(w.end - w.start);
        float back = len > 0 ? kDistEpsilon / len : 0.0f;
        w.result.fraction = std::max(0.0f, midf - back);
        w.result.normal = side == 0 ? plane.normal : -plane.normal;
    }
    return true;
}

} // namespace

TraceResult traceLine(const BspData& bsp, const Vec3& start, const Vec3& end, uint32_t mask) {
    TraceWork w{&bsp, mask, start, end, {}};
    if (bsp.nodes.empty()) {
        w.result.endPos = end;
        return w.result;
    }
    if (recurse(w, 0, 0.0f, 1.0f, start, end) && !w.positioned) {
        // Never left solid: the start point is inside a wall.
        w.result.startSolid = true;
        w.result.fraction = 0.0f;
    }
    w.result.endPos = start + (end - start) * w.result.fraction;
    return w.result;
}

} // namespace k::bsp

// ---- Box traces -------------------------------------------------------------------

namespace k::bsp {
namespace {

// Gap kept between a box and the surface it stops against (Source's DIST_EPSILON).
constexpr float kSurfaceClipEpsilon = 0.03125f;
// A box exactly touching a plane counts as outside it, so something resting on the
// floor (or placed exactly on it by setpos / a spawn point) isn't "stuck".
constexpr float kTouchEpsilon = 0.01f;

struct BoxWork {
    const BspData* bsp;
    uint32_t mask;
    Vec3 start, end;   // box center path
    Vec3 extents;      // half size
    TraceResult result;
    std::vector<uint32_t>* checked;
    uint32_t checkId;
};

uint32_t& checkCounter() {
    thread_local uint32_t counter = 0;
    return counter;
}

std::vector<uint32_t>& checkMarks() {
    thread_local std::vector<uint32_t> marks;
    return marks;
}

float boxRadius(const Vec3& ext, const Vec3& n) {
    return std::fabs(ext.x * n.x) + std::fabs(ext.y * n.y) + std::fabs(ext.z * n.z);
}

void clipBoxToBrush(BoxWork& w, const Brush& brush) {
    if (brush.numSides == 0) return;
    float enterFrac = -1.0f, leaveFrac = 1.0f;
    const Plane* clipPlane = nullptr;
    bool getOut = false, startOut = false;

    for (uint32_t i = 0; i < brush.numSides; ++i) {
        const Plane& plane = w.bsp->planes[static_cast<size_t>(w.bsp->brushSides[brush.firstSide + i].plane)];
        float dist = plane.dist + boxRadius(w.extents, plane.normal);
        float d1 = glm::dot(w.start, plane.normal) - dist;
        float d2 = glm::dot(w.end, plane.normal) - dist;
        bool d1Out = d1 > -kTouchEpsilon; // outside or touching
        if (d2 > 0) getOut = true;
        if (d1Out) startOut = true;
        // Starts outside (or touching) this face and doesn't move into it: the box can never
        // be inside the brush, so the brush can't block this move.
        if (d1Out && (d2 >= d1 || (d1 > 0 && d2 >= kSurfaceClipEpsilon))) return;
        if (!d1Out && d2 <= 0) continue; // inside this face for the whole move
        if (d1 > d2) {
            float f = (d1 - kSurfaceClipEpsilon) / (d1 - d2);
            if (f > enterFrac) {
                enterFrac = f;
                clipPlane = &plane;
            }
        } else {
            float f = (d1 + kSurfaceClipEpsilon) / (d1 - d2);
            if (f < leaveFrac) leaveFrac = f;
        }
    }

    if (!startOut) {
        w.result.startSolid = true;
        w.result.contents |= brush.contents;
        if (!getOut) {
            w.result.allSolid = true;
            w.result.fraction = 0.0f;
        }
        return;
    }
    if (enterFrac < leaveFrac && enterFrac > -1.0f && enterFrac < w.result.fraction && clipPlane) {
        w.result.fraction = std::max(enterFrac, 0.0f);
        w.result.normal = clipPlane->normal;
        w.result.contents = brush.contents;
    }
}

void traceLeaf(BoxWork& w, const Leaf& leaf) {
    for (uint32_t i = 0; i < leaf.numLeafBrushes; ++i) {
        uint32_t b = w.bsp->leafBrushes[leaf.firstLeafBrush + i];
        auto& marks = *w.checked;
        if (marks[b] == w.checkId) continue;
        marks[b] = w.checkId;
        const Brush& brush = w.bsp->brushes[b];
        if (!(brush.contents & w.mask)) continue;
        clipBoxToBrush(w, brush);
        if (w.result.allSolid) return;
    }
}

void traceBoxNode(BoxWork& w, int node, float p1f, float p2f, const Vec3& p1, const Vec3& p2) {
    if (w.result.fraction <= p1f) return; // already hit something nearer
    if (node < 0) {
        traceLeaf(w, w.bsp->leaves[static_cast<size_t>(-1 - node)]);
        return;
    }
    const Node& n = w.bsp->nodes[static_cast<size_t>(node)];
    const Plane& plane = w.bsp->planes[static_cast<size_t>(n.plane)];
    float t1 = plane.distance(p1), t2 = plane.distance(p2);
    float offset = boxRadius(w.extents, plane.normal);
    if (t1 >= offset + 1 && t2 >= offset + 1) {
        traceBoxNode(w, n.children[0], p1f, p2f, p1, p2);
        return;
    }
    if (t1 < -offset - 1 && t2 < -offset - 1) {
        traceBoxNode(w, n.children[1], p1f, p2f, p1, p2);
        return;
    }
    // The box straddles the plane somewhere along the move: visit both sides, near side first.
    int side;
    float frac1, frac2;
    if (t1 < t2) {
        float idist = 1.0f / (t1 - t2);
        side = 1;
        frac2 = (t1 + offset + kSurfaceClipEpsilon) * idist;
        frac1 = (t1 - offset + kSurfaceClipEpsilon) * idist;
    } else if (t1 > t2) {
        float idist = 1.0f / (t1 - t2);
        side = 0;
        frac2 = (t1 - offset - kSurfaceClipEpsilon) * idist;
        frac1 = (t1 + offset + kSurfaceClipEpsilon) * idist;
    } else {
        side = 0;
        frac1 = 1.0f;
        frac2 = 0.0f;
    }
    frac1 = std::clamp(frac1, 0.0f, 1.0f);
    frac2 = std::clamp(frac2, 0.0f, 1.0f);

    float midf = p1f + (p2f - p1f) * frac1;
    Vec3 mid = p1 + (p2 - p1) * frac1;
    traceBoxNode(w, n.children[side], p1f, midf, p1, mid);

    midf = p1f + (p2f - p1f) * frac2;
    mid = p1 + (p2 - p1) * frac2;
    traceBoxNode(w, n.children[side ^ 1], midf, p2f, mid, p2);
}

} // namespace

TraceResult traceBox(const BspData& bsp, const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                     uint32_t mask) {
    // Work with a symmetric box around its center.
    Vec3 offset = (mins + maxs) * 0.5f;
    BoxWork w;
    w.bsp = &bsp;
    w.mask = mask;
    w.start = start + offset;
    w.end = end + offset;
    w.extents = (maxs - mins) * 0.5f;
    w.result = {};
    auto& marks = checkMarks();
    if (marks.size() < bsp.brushes.size()) marks.assign(bsp.brushes.size(), 0);
    w.checked = &marks;
    w.checkId = ++checkCounter();
    if (w.checkId == 0) { // wrapped
        std::fill(marks.begin(), marks.end(), 0);
        w.checkId = ++checkCounter();
    }

    if (!bsp.nodes.empty()) traceBoxNode(w, 0, 0.0f, 1.0f, w.start, w.end);
    else if (!bsp.leaves.empty()) traceLeaf(w, bsp.leaves[0]);

    if (w.result.allSolid) w.result.fraction = 0.0f;
    w.result.endPos = start + (end - start) * w.result.fraction;
    return w.result;
}

} // namespace k::bsp
