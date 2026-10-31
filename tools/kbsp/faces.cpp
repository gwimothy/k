// Visible faces, per-leaf collision brushes, and conversion of everything to BspData.
//
// Faces are made by pushing every drawable brush side down the tree: pieces that land in
// solid leaves are hidden (inside another brush or outside the map) and are dropped. This
// gives CSG for free.

#include "compiler.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace kbsp {
namespace {

void filterFace(Compiler& c, int nodeIdx, Winding w, int texInfo, int plane, int entity) {
    TreeNode& node = c.nodes[static_cast<size_t>(nodeIdx)];
    if (node.leaf) {
        if (node.contents & k::bsp::CONTENTS_SOLID) return;
        if (isTiny(w)) return;
        c.faces.push_back({std::move(w), texInfo, plane, entity});
        c.nodes[static_cast<size_t>(nodeIdx)].faces.push_back(static_cast<int>(c.faces.size()) - 1);
        return;
    }
    const PlaneD& np = c.planes[static_cast<size_t>(node.plane)];
    int front = node.children[0], back = node.children[1];
    switch (classify(w, np)) {
        case Side::Front: filterFace(c, front, std::move(w), texInfo, plane, entity); break;
        case Side::Back: filterFace(c, back, std::move(w), texInfo, plane, entity); break;
        case Side::On: {
            bool sameFacing = glm::dot(c.planes[static_cast<size_t>(plane)].normal, np.normal) > 0;
            filterFace(c, sameFacing ? front : back, std::move(w), texInfo, plane, entity);
            break;
        }
        case Side::Cross: {
            Winding f, b;
            splitWinding(w, np, kOnEpsilon, f, b);
            if (!f.empty()) filterFace(c, front, std::move(f), texInfo, plane, entity);
            if (!b.empty()) filterFace(c, back, std::move(b), texInfo, plane, entity);
            break;
        }
    }
}

void filterBrush(Compiler& c, int nodeIdx, int brushIdx) {
    TreeNode& node = c.nodes[static_cast<size_t>(nodeIdx)];
    if (node.leaf) {
        node.brushes.push_back(brushIdx);
        return;
    }
    const PlaneD& p = c.planes[static_cast<size_t>(node.plane)];
    bool front = false, back = false;
    for (auto& s : c.brushes[static_cast<size_t>(brushIdx)].sides)
        for (auto& v : s.winding) {
            double d = p.distance(v);
            if (d > kOnEpsilon) front = true;
            if (d < -kOnEpsilon) back = true;
        }
    int f = node.children[0], b = node.children[1];
    if (front || !back) filterBrush(c, f, brushIdx);
    if (back) filterBrush(c, b, brushIdx);
}

bool isWorldEntity(const MapEntity& e, size_t index) {
    return index == 0 || e.get("classname") == "func_detail";
}

int emitNode(Compiler& c, int nodeIdx) {
    TreeNode& n = c.nodes[static_cast<size_t>(nodeIdx)];
    auto& bsp = c.bsp;
    if (n.leaf) {
        k::bsp::Leaf leaf{};
        leaf.contents = n.contents;
        leaf.mins = k::Vec3(n.mins);
        leaf.maxs = k::Vec3(n.maxs);
        leaf.firstLeafFace = static_cast<uint32_t>(bsp.leafFaces.size());
        for (int f : n.faces) bsp.leafFaces.push_back(static_cast<uint32_t>(c.faces[static_cast<size_t>(f)].bspFace));
        leaf.numLeafFaces = static_cast<uint32_t>(n.faces.size());
        leaf.firstLeafBrush = static_cast<uint32_t>(bsp.leafBrushes.size());
        for (int b : n.brushes) bsp.leafBrushes.push_back(static_cast<uint32_t>(b));
        leaf.numLeafBrushes = static_cast<uint32_t>(n.brushes.size());
        leaf.visOffset = -1;
        n.outputIndex = static_cast<int>(bsp.leaves.size());
        bsp.leaves.push_back(leaf);
        return -1 - n.outputIndex;
    }
    int index = static_cast<int>(bsp.nodes.size());
    n.outputIndex = index;
    bsp.nodes.push_back({});
    int front = emitNode(c, n.children[0]);
    int back = emitNode(c, n.children[1]);
    // Re-fetch: vectors may have grown.
    TreeNode& nn = c.nodes[static_cast<size_t>(nodeIdx)];
    k::bsp::Node out{};
    out.plane = nn.plane;
    out.children[0] = front;
    out.children[1] = back;
    // Node bounds = union of children.
    auto childBounds = [&](int child, k::Vec3& lo, k::Vec3& hi) {
        if (child >= 0) { lo = bsp.nodes[static_cast<size_t>(child)].mins; hi = bsp.nodes[static_cast<size_t>(child)].maxs; }
        else { lo = bsp.leaves[static_cast<size_t>(-1 - child)].mins; hi = bsp.leaves[static_cast<size_t>(-1 - child)].maxs; }
    };
    k::Vec3 l0, h0, l1, h1;
    childBounds(front, l0, h0);
    childBounds(back, l1, h1);
    out.mins = glm::min(l0, l1);
    out.maxs = glm::max(h0, h1);
    bsp.nodes[static_cast<size_t>(index)] = out;
    return index;
}

void emitFace(Compiler& c, OutFace& f) {
    auto& bsp = c.bsp;
    const TexInfo& ti = c.texInfos[static_cast<size_t>(f.texInfo)];
    const MapSide& side = c.mapSides[static_cast<size_t>(ti.side)];
    const PlaneD& plane = c.planes[static_cast<size_t>(f.plane)];

    // Wind counter-clockwise around the plane normal.
    Winding w = f.winding;
    if (w.size() >= 3 && glm::dot(glm::cross(w[1] - w[0], w[2] - w[0]), plane.normal) < 0)
        std::reverse(w.begin(), w.end());
    f.winding = w;

    k::bsp::Face out{};
    out.material = ti.material;
    out.plane = f.plane;
    out.flags = ti.flags;
    out.firstIndex = static_cast<uint32_t>(bsp.indices.size());
    auto base = static_cast<uint32_t>(bsp.vertices.size());
    for (auto& p : w) {
        k::bsp::Vertex v{};
        v.position = k::Vec3(p);
        v.normal = k::Vec3(plane.normal);
        glm::dvec2 tc = textureCoords(side, plane.normal, p);
        v.uv = k::Vec2(static_cast<float>(tc.x / ti.texWidth), static_cast<float>(tc.y / ti.texHeight));
        bsp.vertices.push_back(v);
    }
    for (uint32_t i = 2; i < w.size(); ++i) bsp.indices.insert(bsp.indices.end(), {base, base + i - 1, base + i});
    out.numIndices = static_cast<uint32_t>(bsp.indices.size()) - out.firstIndex;
    f.bspFace = static_cast<int>(bsp.faces.size());
    bsp.faces.push_back(out);
}

} // namespace

void Compiler::makeFaces() {
    for (auto& b : brushes) {
        if (!isWorldEntity(entities[static_cast<size_t>(b.entity)], static_cast<size_t>(b.entity))) continue;
        for (auto& s : b.sides) {
            if (s.texInfo < 0 || (texInfos[static_cast<size_t>(s.texInfo)].flags & k::bsp::FACE_NODRAW)) continue;
            filterFace(*this, root, s.winding, s.texInfo, s.plane, 0);
        }
    }
    info("%zu world faces", faces.size());
}

void Compiler::assignLeafBrushes() {
    for (size_t i = 0; i < brushes.size(); ++i)
        if (isWorldEntity(entities[static_cast<size_t>(brushes[i].entity)], static_cast<size_t>(brushes[i].entity)))
            filterBrush(*this, root, static_cast<int>(i));
}

void Compiler::emitBsp() {
    bsp = {};
    // Axial bevel planes for collision: box traces expand each brush plane by the box size,
    // which only matches the true shape if every brush also has its six axis-aligned
    // bounding planes (Quake 3 does the same). Boxes already have them; ramps don't.
    std::vector<std::vector<int>> bevels(brushes.size());
    for (size_t i = 0; i < brushes.size(); ++i) {
        const Brush& b = brushes[i];
        for (int axis = 0; axis < 3; ++axis)
            for (int dir = -1; dir <= 1; dir += 2) {
                dvec3 n(0);
                n[axis] = dir;
                double d = dir > 0 ? b.maxs[axis] : -b.mins[axis];
                int p = findPlane(n, d);
                bool has = false;
                for (auto& s : b.sides) has |= s.plane == p;
                if (!has) bevels[i].push_back(p);
            }
    }
    for (auto& p : planes) bsp.planes.push_back({k::Vec3(p.normal), static_cast<float>(p.dist)});

    // World faces first (model 0), then brush entities.
    for (auto& f : faces) emitFace(*this, f);
    k::bsp::Model world{};
    world.mins = k::Vec3(worldMins);
    world.maxs = k::Vec3(worldMaxs);
    world.firstFace = 0;
    world.numFaces = static_cast<uint32_t>(bsp.faces.size());
    world.headNode = 0;
    bsp.models.push_back(world);

    std::vector<k::bsp::Entity> outEnts;
    for (size_t e = 0; e < entities.size(); ++e) {
        const MapEntity& me = entities[e];
        if (e != 0 && me.get("classname") == "func_detail") continue; // merged into the world
        k::bsp::Entity ent(me.keys.begin(), me.keys.end());
        if (e != 0 && !me.brushes.empty()) {
            k::bsp::Model m{};
            m.firstFace = static_cast<uint32_t>(bsp.faces.size());
            dvec3 lo(1e30), hi(-1e30);
            for (auto& b : brushes) {
                if (b.entity != static_cast<int>(e)) continue;
                lo = glm::min(lo, b.mins);
                hi = glm::max(hi, b.maxs);
                for (auto& s : b.sides) {
                    if (s.texInfo < 0 || (texInfos[static_cast<size_t>(s.texInfo)].flags & k::bsp::FACE_NODRAW)) continue;
                    faces.push_back({s.winding, s.texInfo, s.plane, static_cast<int>(e)});
                    emitFace(*this, faces.back());
                }
            }
            m.numFaces = static_cast<uint32_t>(bsp.faces.size()) - m.firstFace;
            m.mins = k::Vec3(lo);
            m.maxs = k::Vec3(hi);
            m.headNode = -1;
            ent["model"] = "*" + std::to_string(bsp.models.size());
            bsp.models.push_back(m);
        }
        outEnts.push_back(std::move(ent));
    }
    bsp.entities = std::move(outEnts);

    emitNode(*this, root);

    for (auto& b : brushes) {
        k::bsp::Brush ob{};
        ob.firstSide = static_cast<uint32_t>(bsp.brushSides.size());
        ob.contents = b.contents;
        for (auto& s : b.sides)
            bsp.brushSides.push_back({s.plane, s.texInfo >= 0 ? texInfos[static_cast<size_t>(s.texInfo)].material : 0u});
        for (int p : bevels[static_cast<size_t>(&b - brushes.data())]) bsp.brushSides.push_back({p, 0u});
        ob.numSides = static_cast<uint32_t>(bsp.brushSides.size()) - ob.firstSide;
        bsp.brushes.push_back(ob);
    }
    bsp.materials = materials;
    info("%zu faces, %zu vertices, %zu triangles, %zu leaves", bsp.faces.size(), bsp.vertices.size(),
         bsp.indices.size() / 3, bsp.leaves.size());
}

} // namespace kbsp

namespace kbsp {

bool Compiler::writeOutput() {
    auto bytes = k::bsp::write(bsp);
    std::error_code ec;
    std::filesystem::create_directories(opt.output.parent_path(), ec);
    std::ofstream f(opt.output, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        warn("couldn't write %s", opt.output.string().c_str());
        return false;
    }
    info("wrote %s (%zu KB)", opt.output.string().c_str(), bytes.size() / 1024);
    return true;
}

} // namespace kbsp
