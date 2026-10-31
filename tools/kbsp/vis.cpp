// Potentially visible sets.
//
// For each leaf, flood through portals keeping only those that could be seen through the
// first portal: the next portal must lie (partly) beyond the first portal's plane, and the
// first portal must lie (partly) behind the next one's plane. This is Quake's "base vis"
// pass (vvis -fast in Source): conservative, never hides something visible. Full
// anti-penumbra clipping can tighten it later without changing the file format.

#include "compiler.h"

#include <atomic>
#include <thread>

namespace kbsp {
namespace {

struct DirPortal {
    PlaneD plane;  // faces into `to`
    const Winding* winding;
    int from, to;  // output leaf indices
};

bool anyFront(const Winding& w, const PlaneD& p) {
    for (auto& v : w)
        if (p.distance(v) > kOnEpsilon) return true;
    return false;
}

} // namespace

void Compiler::computeVis() {
    size_t numLeaves = bsp.leaves.size();
    std::vector<DirPortal> dps;
    std::vector<std::vector<int>> leafPortals(numLeaves);
    for (auto& p : portals) {
        if (!p.alive || p.nodes[0] == outsideNode || p.nodes[1] == outsideNode) continue;
        const TreeNode& a = nodes[static_cast<size_t>(p.nodes[0])];
        const TreeNode& b = nodes[static_cast<size_t>(p.nodes[1])];
        if (!a.leaf || !b.leaf || (a.contents & k::bsp::CONTENTS_SOLID) || (b.contents & k::bsp::CONTENTS_SOLID)) continue;
        int la = a.outputIndex, lb = b.outputIndex;
        dps.push_back({p.plane, &p.winding, lb, la});          // back -> front
        leafPortals[static_cast<size_t>(lb)].push_back(static_cast<int>(dps.size()) - 1);
        dps.push_back({p.plane.flipped(), &p.winding, la, lb}); // front -> back
        leafPortals[static_cast<size_t>(la)].push_back(static_cast<int>(dps.size()) - 1);
    }

    // mightsee per directional portal, as a leaf bitset.
    std::vector<std::vector<uint8_t>> seen(dps.size());
    std::atomic<size_t> next{0};
    auto worker = [&] {
        std::vector<int> stack;
        for (size_t i; (i = next++) < dps.size();) {
            const DirPortal& src = dps[i];
            std::vector<uint8_t> vis(numLeaves, 0);
            vis[static_cast<size_t>(src.to)] = 1;
            stack.assign(1, src.to);
            while (!stack.empty()) {
                int leaf = stack.back();
                stack.pop_back();
                for (int qi : leafPortals[static_cast<size_t>(leaf)]) {
                    const DirPortal& q = dps[static_cast<size_t>(qi)];
                    if (vis[static_cast<size_t>(q.to)] || q.to == src.from) continue;
                    if (!anyFront(*q.winding, src.plane)) continue;            // q is beyond src
                    if (!anyFront(*src.winding, q.plane.flipped())) continue;  // src is behind q
                    vis[static_cast<size_t>(q.to)] = 1;
                    stack.push_back(q.to);
                }
            }
            seen[i] = std::move(vis);
        }
    };
    unsigned n = opt.threads > 0 ? static_cast<unsigned>(opt.threads) : std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < n; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    size_t rowBytes = (numLeaves + 7) / 8;
    size_t totalVisible = 0, emptyLeaves = 0;
    bsp.visData.clear();
    for (size_t l = 0; l < numLeaves; ++l) {
        auto& leaf = bsp.leaves[l];
        if (leaf.contents & k::bsp::CONTENTS_SOLID) {
            leaf.visOffset = -1;
            continue;
        }
        std::vector<uint8_t> bits(rowBytes, 0);
        auto set = [&](size_t i) { bits[i >> 3] |= static_cast<uint8_t>(1u << (i & 7)); };
        set(l);
        size_t count = 1;
        std::vector<uint8_t> merged(numLeaves, 0);
        merged[l] = 1;
        for (int pi : leafPortals[l]) {
            const auto& s = seen[static_cast<size_t>(pi)];
            for (size_t j = 0; j < numLeaves; ++j)
                if (s[j] && !merged[j]) {
                    merged[j] = 1;
                    set(j);
                    ++count;
                }
        }
        leaf.visOffset = static_cast<int32_t>(bsp.visData.size());
        auto packed = k::bsp::compressVis(bits);
        bsp.visData.insert(bsp.visData.end(), packed.begin(), packed.end());
        totalVisible += count;
        ++emptyLeaves;
    }
    info("vis: %zu portals, average %.1f of %zu empty leaves visible, %zu bytes", dps.size() / 2,
         emptyLeaves ? static_cast<double>(totalVisible) / static_cast<double>(emptyLeaves) : 0.0, emptyLeaves,
         bsp.visData.size());
}

} // namespace kbsp
