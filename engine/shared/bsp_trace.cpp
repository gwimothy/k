#include "shared/bsp_trace.h"

#include <algorithm>

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
