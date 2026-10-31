// Solid-leaf BSP construction from structural brushes (after q3map's BrushBSP).
//
// Each node splits on a brush side plane that hasn't been used yet on the path from the
// root. Brushes are split into fragments. When every side of every fragment in a node has
// been used, the node is a leaf, and it is solid exactly when a fragment remains (the
// fragments then fill the leaf completely).

#include "compiler.h"

#include <cmath>
#include <limits>

namespace kbsp {
namespace {

bool isAxial(const PlaneD& p) {
    dvec3 a = glm::abs(p.normal);
    return (a.x == 1 || a.y == 1 || a.z == 1);
}

double brushVolume(const Brush& b) {
    if (b.sides.empty()) return 0;
    // Sum of pyramids from one corner.
    const Winding* first = nullptr;
    for (auto& s : b.sides)
        if (!s.winding.empty()) { first = &s.winding; break; }
    if (!first) return 0;
    dvec3 corner = (*first)[0];
    double vol = 0;
    for (auto& s : b.sides) {
        if (s.winding.empty()) continue;
        // distance from corner to the side plane (negative, sides face outward)
        dvec3 n = glm::cross(s.winding.size() > 2 ? s.winding[2] - s.winding[1] : dvec3(0),
                             s.winding.size() > 2 ? s.winding[0] - s.winding[1] : dvec3(0));
        double len = glm::length(n);
        if (len < 1e-12) continue;
        n /= len;
        double d = -glm::dot(corner - s.winding[0], n);
        vol += d * area(s.winding);
    }
    return std::fabs(vol / 3.0);
}

enum { kFront = 1, kBack = 2 };

int brushPlaneSide(const Brush& b, const PlaneD& p) {
    // Quick bounds test, then exact points.
    int side = 0;
    for (auto& s : b.sides)
        for (auto& v : s.winding) {
            double d = p.distance(v);
            if (d > kOnEpsilon) side |= kFront;
            if (d < -kOnEpsilon) side |= kBack;
            if (side == (kFront | kBack)) return side;
        }
    return side;
}

void splitBrush(const Compiler& c, const Brush& b, int planeIdx, Brush& front, Brush& back, bool& hasFront,
                bool& hasBack) {
    const PlaneD& plane = c.planes[static_cast<size_t>(planeIdx)];
    hasFront = hasBack = false;
    int side = brushPlaneSide(b, plane);
    if (side == kFront || side == 0) {
        // 0: every point on the plane (degenerate); treat as front.
        front = b;
        hasFront = true;
        return;
    }
    if (side == kBack) {
        back = b;
        hasBack = true;
        return;
    }

    // The new face on the split plane: plane polygon clipped to the inside of the brush.
    Winding mid = baseWinding(plane);
    for (auto& s : b.sides) {
        if (mid.empty()) break;
        mid = clipFront(mid, c.planes[static_cast<size_t>(s.plane ^ 1)], 0.0);
    }
    if (mid.empty() || isTiny(mid)) {
        // Barely touching: put it on the side with more of the brush.
        double dfront = 0, dback = 0;
        for (auto& s : b.sides)
            for (auto& v : s.winding) {
                double d = plane.distance(v);
                dfront = std::max(dfront, d);
                dback = std::min(dback, d);
            }
        if (dfront > -dback) { front = b; hasFront = true; }
        else { back = b; hasBack = true; }
        return;
    }

    Brush parts[2];
    for (int i = 0; i < 2; ++i) {
        parts[i].contents = b.contents;
        parts[i].original = b.original;
        parts[i].entity = b.entity;
    }
    for (auto& s : b.sides) {
        Winding f, bk;
        splitWinding(s.winding, plane, 0.0, f, bk);
        if (f.empty() && bk.empty()) continue; // coplanar side: dropped, replaced by mid
        if (!f.empty()) parts[0].sides.push_back({s.plane, f, s.texInfo, s.tested});
        if (!bk.empty()) parts[1].sides.push_back({s.plane, bk, s.texInfo, s.tested});
    }
    // Front fragment's new side faces back (outward from the fragment): flipped plane.
    parts[0].sides.push_back({planeIdx ^ 1, Winding(mid.rbegin(), mid.rend()), -1, true});
    parts[1].sides.push_back({planeIdx, mid, -1, true});

    for (int i = 0; i < 2; ++i) {
        parts[i].computeBounds();
        if (parts[i].sides.size() < 4 || brushVolume(parts[i]) < 1.0) continue;
        if (i == 0) { front = std::move(parts[0]); hasFront = true; }
        else { back = std::move(parts[1]); hasBack = true; }
    }
}

int chooseSplitter(const Compiler& c, const std::vector<Brush>& list) {
    int best = -1;
    double bestValue = std::numeric_limits<double>::max();
    std::vector<char> tried(c.planes.size() / 2 + 1, 0);
    for (int pass = 0; pass < 2 && best < 0; ++pass) {
        // Pass 0: axial planes only (cheap, robust). Pass 1: anything.
        for (auto& b : list) {
            for (auto& s : b.sides) {
                if (s.tested || s.winding.empty()) continue;
                int pidx = s.plane & ~1;
                if (tried[static_cast<size_t>(pidx / 2)] == pass + 1) continue;
                tried[static_cast<size_t>(pidx / 2)] = static_cast<char>(pass + 1);
                const PlaneD& p = c.planes[static_cast<size_t>(pidx)];
                if (pass == 0 && !isAxial(p)) continue;
                int front = 0, back = 0, splits = 0, facing = 0;
                for (auto& o : list) {
                    int side = brushPlaneSide(o, p);
                    if (side == (kFront | kBack)) ++splits;
                    else if (side == kFront) ++front;
                    else if (side == kBack) ++back;
                    for (auto& os : o.sides)
                        if ((os.plane & ~1) == pidx) ++facing;
                }
                double value = 5.0 * splits + std::abs(front - back) - 3.0 * facing;
                if (value < bestValue) {
                    bestValue = value;
                    best = pidx;
                }
            }
        }
    }
    return best;
}

int buildNode(Compiler& c, std::vector<Brush> list, const dvec3& mins, const dvec3& maxs) {
    int idx = static_cast<int>(c.nodes.size());
    c.nodes.emplace_back();
    c.nodes[static_cast<size_t>(idx)].mins = mins;
    c.nodes[static_cast<size_t>(idx)].maxs = maxs;

    int plane = chooseSplitter(c, list);
    if (plane < 0) {
        TreeNode& leaf = c.nodes[static_cast<size_t>(idx)];
        leaf.leaf = true;
        for (auto& b : list) leaf.contents |= b.contents;
        return idx;
    }

    std::vector<Brush> frontList, backList;
    for (auto& b : list) {
        Brush f, bk;
        bool hf, hb;
        splitBrush(c, b, plane, f, bk, hf, hb);
        // Sides lying on the splitter are used up in whichever fragment kept them.
        auto mark = [&](Brush& br) {
            for (auto& s : br.sides)
                if ((s.plane & ~1) == plane) s.tested = true;
        };
        if (hf) { mark(f); frontList.push_back(std::move(f)); }
        if (hb) { mark(bk); backList.push_back(std::move(bk)); }
    }

    // Child bounds: clip the node box by the plane when axial (good enough for culling).
    const PlaneD& p = c.planes[static_cast<size_t>(plane)];
    dvec3 fmins = mins, fmaxs = maxs, bmins = mins, bmaxs = maxs;
    if (isAxial(p)) {
        int axis = std::fabs(p.normal.x) == 1 ? 0 : std::fabs(p.normal.y) == 1 ? 1 : 2;
        double d = p.dist * p.normal[axis]; // normal is +1 for canonical planes
        fmins[axis] = std::max(fmins[axis], d);
        bmaxs[axis] = std::min(bmaxs[axis], d);
    }
    c.nodes[static_cast<size_t>(idx)].plane = plane;
    int front = buildNode(c, std::move(frontList), fmins, fmaxs);
    int back = buildNode(c, std::move(backList), bmins, bmaxs);
    c.nodes[static_cast<size_t>(idx)].children[0] = front;
    c.nodes[static_cast<size_t>(idx)].children[1] = back;
    return idx;
}

} // namespace

void Compiler::buildTree() {
    std::vector<Brush> structural;
    for (auto& b : brushes)
        if (b.entity == 0 && (b.contents & k::bsp::CONTENTS_SOLID) && !(b.contents & k::bsp::CONTENTS_DETAIL))
            structural.push_back(b);
    dvec3 pad(64);
    root = buildNode(*this, std::move(structural), worldMins - pad, worldMaxs + pad);
    size_t leaves = 0, solid = 0;
    for (auto& n : nodes)
        if (n.leaf) {
            ++leaves;
            if (n.contents & k::bsp::CONTENTS_SOLID) ++solid;
        }
    info("BSP: %zu nodes, %zu leaves (%zu solid)", nodes.size() - leaves, leaves, solid);
}

int Compiler::leafForPoint(const dvec3& p) const {
    int n = root;
    while (n >= 0 && !nodes[static_cast<size_t>(n)].leaf) {
        const TreeNode& node = nodes[static_cast<size_t>(n)];
        n = node.children[planes[static_cast<size_t>(node.plane)].distance(p) >= 0 ? 0 : 1];
    }
    return n;
}

} // namespace kbsp
