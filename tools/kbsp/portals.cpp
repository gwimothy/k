// Portals: the convex openings between neighbouring leaves (Quake's MakeTreePortals).
//
// The root gets six portals to a virtual "outside" node, built from the world bounds.
// Each node then creates a portal on its plane (clipped by the portals that bound it) and
// splits its existing portals between its children.

#include "compiler.h"
#include "core/keyvalues.h"

#include <deque>

namespace kbsp {
namespace {

void addPortal(Compiler& c, int p) {
    for (int i = 0; i < 2; ++i) c.nodes[static_cast<size_t>(c.portals[static_cast<size_t>(p)].nodes[i])].portals.push_back(p);
}

void removePortalFrom(Compiler& c, int node, int p) {
    auto& v = c.nodes[static_cast<size_t>(node)].portals;
    std::erase(v, p);
}

void makeNodePortal(Compiler& c, int nodeIdx) {
    TreeNode& node = c.nodes[static_cast<size_t>(nodeIdx)];
    Winding w = baseWinding(c.planes[static_cast<size_t>(node.plane)]);
    for (int pidx : node.portals) {
        const Portal& p = c.portals[static_cast<size_t>(pidx)];
        // Keep the side of the portal plane this node is on.
        PlaneD clip = p.nodes[0] == nodeIdx ? p.plane : p.plane.flipped();
        w = clipFront(w, clip, 0.0);
        if (w.empty()) break;
    }
    if (w.empty() || isTiny(w)) return;
    Portal np;
    np.plane = c.planes[static_cast<size_t>(node.plane)];
    np.winding = std::move(w);
    np.nodes[0] = node.children[0];
    np.nodes[1] = node.children[1];
    c.portals.push_back(std::move(np));
    addPortal(c, static_cast<int>(c.portals.size()) - 1);
}

void splitNodePortals(Compiler& c, int nodeIdx) {
    const PlaneD plane = c.planes[static_cast<size_t>(c.nodes[static_cast<size_t>(nodeIdx)].plane)];
    int front = c.nodes[static_cast<size_t>(nodeIdx)].children[0];
    int back = c.nodes[static_cast<size_t>(nodeIdx)].children[1];
    std::vector<int> list = c.nodes[static_cast<size_t>(nodeIdx)].portals;
    for (int pidx : list) {
        Portal& p = c.portals[static_cast<size_t>(pidx)];
        if (p.nodes[0] == front && p.nodes[1] == back) continue; // the node's own new portal
        int side = p.nodes[0] == nodeIdx ? 0 : 1;
        int other = p.nodes[side ^ 1];
        removePortalFrom(c, p.nodes[0], pidx);
        removePortalFrom(c, p.nodes[1], pidx);

        Winding fw, bw;
        splitWinding(p.winding, plane, 0.0, fw, bw);
        if (fw.empty() && bw.empty()) {
            // Coplanar with the splitter: shouldn't happen (each plane is used once per path).
            p.alive = false;
            continue;
        }
        if (!fw.empty() && isTiny(fw)) fw.clear();
        if (!bw.empty() && isTiny(bw)) bw.clear();

        if (!fw.empty() && !bw.empty()) {
            // Reuse this portal for the front piece, make a new one for the back piece.
            Portal backPortal = p;
            backPortal.winding = std::move(bw);
            backPortal.nodes[side] = back;
            backPortal.nodes[side ^ 1] = other;
            p.winding = std::move(fw);
            p.nodes[side] = front;
            p.nodes[side ^ 1] = other;
            addPortal(c, pidx);
            c.portals.push_back(std::move(backPortal));
            addPortal(c, static_cast<int>(c.portals.size()) - 1);
        } else if (!fw.empty()) {
            p.winding = std::move(fw);
            p.nodes[side] = front;
            addPortal(c, pidx);
        } else if (!bw.empty()) {
            p.winding = std::move(bw);
            p.nodes[side] = back;
            addPortal(c, pidx);
        } else {
            p.alive = false;
        }
    }
    c.nodes[static_cast<size_t>(nodeIdx)].portals.clear();
}

void makeTreePortals(Compiler& c, int nodeIdx) {
    if (c.nodes[static_cast<size_t>(nodeIdx)].leaf) return;
    makeNodePortal(c, nodeIdx);
    splitNodePortals(c, nodeIdx);
    int front = c.nodes[static_cast<size_t>(nodeIdx)].children[0];
    int back = c.nodes[static_cast<size_t>(nodeIdx)].children[1];
    makeTreePortals(c, front);
    makeTreePortals(c, back);
}

} // namespace

void Compiler::makePortals() {
    outsideNode = static_cast<int>(nodes.size());
    nodes.emplace_back();
    nodes.back().leaf = true;
    nodes.back().contents = 0;

    dvec3 mins = worldMins - dvec3(128), maxs = worldMaxs + dvec3(128);
    // Six inward-facing box planes; the root is in front of each.
    for (int axis = 0; axis < 3; ++axis) {
        for (int s = 0; s < 2; ++s) {
            PlaneD pl;
            pl.normal = dvec3(0);
            if (s == 0) { pl.normal[axis] = 1; pl.dist = mins[axis]; }
            else { pl.normal[axis] = -1; pl.dist = -maxs[axis]; }
            Portal p;
            p.plane = pl;
            p.winding = baseWinding(pl);
            p.nodes[0] = root;
            p.nodes[1] = outsideNode;
            portals.push_back(std::move(p));
        }
    }
    // Clip each box portal by the other five.
    for (size_t i = 0; i < 6; ++i)
        for (size_t j = 0; j < 6; ++j)
            if (i != j) portals[i].winding = clipFront(portals[i].winding, portals[j].plane, 0.0);
    for (int i = 0; i < 6; ++i) addPortal(*this, i);

    makeTreePortals(*this, root);

    // Leaf bounds from their portals (tight regions for culling).
    for (auto& n : nodes) {
        if (!n.leaf || n.portals.empty()) continue;
        dvec3 lo(1e30), hi(-1e30);
        for (int p : n.portals)
            for (auto& v : portals[static_cast<size_t>(p)].winding) {
                lo = glm::min(lo, v);
                hi = glm::max(hi, v);
            }
        n.mins = glm::max(lo, mins);
        n.maxs = glm::min(hi, maxs);
    }
    size_t alive = 0;
    for (auto& p : portals) alive += p.alive;
    info("%zu portals", alive);
}

bool Compiler::floodFill() {
    std::deque<int> queue;
    int seeds = 0;
    for (size_t e = 1; e < entities.size(); ++e) {
        auto origin = entities[e].get("origin");
        if (origin.empty() || !entities[e].brushes.empty()) continue;
        auto v = k::parseVec3(origin);
        int leaf = leafForPoint(dvec3(v));
        if (leaf < 0 || (nodes[static_cast<size_t>(leaf)].contents & k::bsp::CONTENTS_SOLID)) continue;
        if (!nodes[static_cast<size_t>(leaf)].occupied) {
            nodes[static_cast<size_t>(leaf)].occupied = 1;
            queue.push_back(leaf);
            ++seeds;
        }
    }
    if (!seeds) {
        warn("no entities inside the map to flood from (add an info_player_start)");
        return false;
    }
    while (!queue.empty()) {
        int n = queue.front();
        queue.pop_front();
        for (int pidx : nodes[static_cast<size_t>(n)].portals) {
            const Portal& p = portals[static_cast<size_t>(pidx)];
            int other = p.nodes[0] == n ? p.nodes[1] : p.nodes[0];
            TreeNode& o = nodes[static_cast<size_t>(other)];
            if (o.occupied || (o.contents & k::bsp::CONTENTS_SOLID)) continue;
            o.occupied = nodes[static_cast<size_t>(n)].occupied + 1;
            o.parentPortalFrom = n;
            if (other == outsideNode) {
                // Leak: walk back to an entity and report the path.
                leaked = true;
                std::string path;
                int cur = n;
                int steps = 0;
                while (cur >= 0 && steps++ < 12) {
                    const TreeNode& t = nodes[static_cast<size_t>(cur)];
                    path += " " + fmtPoint((t.mins + t.maxs) * 0.5);
                    cur = t.parentPortalFrom;
                }
                warn("**** LEAK **** the map isn't sealed. Path from the void toward an entity:%s", path.c_str());
                warn("vis and outside removal are disabled until the leak is fixed");
                return false;
            }
            queue.push_back(other);
        }
    }
    return true;
}

void Compiler::fillOutside() {
    size_t filled = 0;
    for (auto& n : nodes) {
        if (!n.leaf || (n.contents & k::bsp::CONTENTS_SOLID) || n.occupied) continue;
        n.contents = k::bsp::CONTENTS_SOLID | k::bsp::CONTENTS_OUTSIDE;
        ++filled;
    }
    info("filled %zu outside leaves", filled);
}

} // namespace kbsp
