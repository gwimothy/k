#include "shared/bspfile.h"

#include "core/keyvalues.h"
#include "net/bytebuffer.h"

#include <cstring>

namespace k::bsp {
namespace {

enum Lump : uint32_t {
    LUMP_ENTITIES, LUMP_PLANES, LUMP_NODES, LUMP_LEAVES, LUMP_VERTICES, LUMP_INDICES, LUMP_FACES,
    LUMP_LEAFFACES, LUMP_BRUSHES, LUMP_BRUSHSIDES, LUMP_LEAFBRUSHES, LUMP_MODELS, LUMP_MATERIALS,
    LUMP_VISIBILITY, LUMP_LIGHTMAP, LUMP_COUNT
};

template <class T>
std::vector<uint8_t> bytesOf(const std::vector<T>& v) {
    std::vector<uint8_t> out(v.size() * sizeof(T));
    if (!v.empty()) std::memcpy(out.data(), v.data(), out.size());
    return out;
}

template <class T>
bool fromBytes(const uint8_t* p, size_t n, std::vector<T>& out) {
    if (n % sizeof(T) != 0) return false;
    out.resize(n / sizeof(T));
    if (n) std::memcpy(out.data(), p, n);
    return true;
}

} // namespace

// ---- Queries ---------------------------------------------------------------------

int BspData::findLeaf(const Vec3& p) const {
    if (nodes.empty()) return leaves.empty() ? -1 : 0;
    int n = 0;
    while (n >= 0) {
        const Node& node = nodes[static_cast<size_t>(n)];
        n = node.children[planes[static_cast<size_t>(node.plane)].distance(p) >= 0 ? 0 : 1];
    }
    return -1 - n;
}

void BspData::decompressVis(int leaf, std::vector<uint8_t>& out) const {
    out.assign(leaves.size(), 1);
    if (leaf < 0 || static_cast<size_t>(leaf) >= leaves.size()) return;
    int32_t off = leaves[static_cast<size_t>(leaf)].visOffset;
    if (off < 0 || static_cast<size_t>(off) >= visData.size()) return; // no vis: everything visible
    std::vector<uint8_t> bits;
    size_t rowBytes = (leaves.size() + 7) / 8;
    decompressVisRow(visData.data() + off, visData.size() - static_cast<size_t>(off), rowBytes, bits);
    for (size_t i = 0; i < leaves.size(); ++i) out[i] = (bits[i >> 3] >> (i & 7)) & 1;
}

uint32_t BspData::pointContents(const Vec3& p) const {
    int leaf = findLeaf(p);
    return leaf < 0 ? CONTENTS_SOLID : leaves[static_cast<size_t>(leaf)].contents;
}

const Entity* BspData::findEntity(const std::string& classname) const {
    for (auto& e : entities) {
        auto it = e.find("classname");
        if (it != e.end() && it->second == classname) return &e;
    }
    return nullptr;
}

// ---- Vis compression ---------------------------------------------------------------

std::vector<uint8_t> compressVis(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) {
            out.push_back(bits[i]);
            continue;
        }
        size_t run = 1;
        while (i + 1 < bits.size() && bits[i + 1] == 0 && run < 255) {
            ++i;
            ++run;
        }
        out.push_back(0);
        out.push_back(static_cast<uint8_t>(run));
    }
    return out;
}

void decompressVisRow(const uint8_t* src, size_t avail, size_t rowBytes, std::vector<uint8_t>& out) {
    out.assign(rowBytes, 0);
    size_t o = 0, i = 0;
    while (o < rowBytes && i < avail) {
        if (src[i]) {
            out[o++] = src[i++];
        } else {
            if (i + 1 >= avail) break;
            o += src[i + 1];
            i += 2;
        }
    }
}

// ---- Entities -----------------------------------------------------------------------

std::string writeEntities(const std::vector<Entity>& ents) {
    std::string out;
    for (auto& e : ents) {
        out += "{\n";
        for (auto& [k, v] : e) out += "\"" + k + "\" \"" + v + "\"\n";
        out += "}\n";
    }
    return out;
}

std::vector<Entity> parseEntities(const std::string& text) {
    // Entity blocks are KeyValues sections without a name: wrap each "{" with a dummy key.
    std::string wrapped;
    wrapped.reserve(text.size() + 64);
    bool inQuote = false;
    for (char c : text) {
        if (c == '"') inQuote = !inQuote;
        if (c == '{' && !inQuote) wrapped += "\"entity\" ";
        wrapped += c;
    }
    std::vector<Entity> ents;
    auto kv = KeyValues::parse(wrapped);
    if (!kv) return ents;
    for (auto& block : kv->children()) {
        Entity e;
        for (auto& pair : block.children()) e[pair.name()] = pair.value();
        ents.push_back(std::move(e));
    }
    return ents;
}

Vec3 entityVec3(const Entity& e, const std::string& key, Vec3 def) {
    auto it = e.find(key);
    return it == e.end() ? def : parseVec3(it->second, def);
}

std::string entityString(const Entity& e, const std::string& key, const std::string& def) {
    auto it = e.find(key);
    return it == e.end() ? def : it->second;
}

float entityFloat(const Entity& e, const std::string& key, float def) {
    auto it = e.find(key);
    return it == e.end() || it->second.empty() ? def : std::strtof(it->second.c_str(), nullptr);
}

// ---- Serialization ------------------------------------------------------------------

std::vector<uint8_t> write(const BspData& b) {
    std::vector<std::vector<uint8_t>> lumps(LUMP_COUNT);
    std::string ents = writeEntities(b.entities);
    lumps[LUMP_ENTITIES].assign(ents.begin(), ents.end());
    lumps[LUMP_PLANES] = bytesOf(b.planes);
    lumps[LUMP_NODES] = bytesOf(b.nodes);
    lumps[LUMP_LEAVES] = bytesOf(b.leaves);
    lumps[LUMP_VERTICES] = bytesOf(b.vertices);
    lumps[LUMP_INDICES] = bytesOf(b.indices);
    lumps[LUMP_FACES] = bytesOf(b.faces);
    lumps[LUMP_LEAFFACES] = bytesOf(b.leafFaces);
    lumps[LUMP_BRUSHES] = bytesOf(b.brushes);
    lumps[LUMP_BRUSHSIDES] = bytesOf(b.brushSides);
    lumps[LUMP_LEAFBRUSHES] = bytesOf(b.leafBrushes);
    lumps[LUMP_MODELS] = bytesOf(b.models);
    {
        net::ByteWriter w;
        w.write(static_cast<uint32_t>(b.materials.size()));
        for (auto& m : b.materials) w.writeString(m);
        lumps[LUMP_MATERIALS] = w.data();
    }
    lumps[LUMP_VISIBILITY] = b.visData;
    {
        net::ByteWriter w;
        w.write(b.lightmapWidth);
        w.write(b.lightmapHeight);
        w.writeBytes({reinterpret_cast<const uint8_t*>(b.lightmap.data()), b.lightmap.size() * sizeof(uint16_t)});
        lumps[LUMP_LIGHTMAP] = w.data();
    }

    net::ByteWriter out;
    out.write(kMagic);
    out.write(kVersion);
    out.write(static_cast<uint32_t>(LUMP_COUNT));
    uint32_t offset = 12 + LUMP_COUNT * 8;
    for (auto& l : lumps) {
        out.write(offset);
        out.write(static_cast<uint32_t>(l.size()));
        offset += static_cast<uint32_t>((l.size() + 3) & ~size_t(3));
    }
    for (auto& l : lumps) {
        out.writeBytes(l);
        for (size_t pad = l.size(); pad % 4; ++pad) out.write<uint8_t>(0);
    }
    return out.take();
}

std::optional<BspData> read(const std::vector<uint8_t>& bytes, std::string* error) {
    auto fail = [&](const char* msg) -> std::optional<BspData> {
        if (error) *error = msg;
        return std::nullopt;
    };
    net::ByteReader r(bytes);
    if (r.read<uint32_t>() != kMagic) return fail("not a .kbsp file");
    uint32_t version = r.read<uint32_t>();
    if (version != kVersion) return fail("unsupported .kbsp version (recompile the map)");
    uint32_t count = r.read<uint32_t>();
    if (count < LUMP_COUNT) return fail("truncated lump directory");
    struct Dir { uint32_t offset, size; };
    std::vector<Dir> dir(count);
    for (auto& d : dir) {
        d.offset = r.read<uint32_t>();
        d.size = r.read<uint32_t>();
        if (static_cast<size_t>(d.offset) + d.size > bytes.size()) return fail("lump out of range");
    }
    if (r.overflowed()) return fail("truncated header");
    auto lump = [&](Lump l) { return bytes.data() + dir[l].offset; };
    auto size = [&](Lump l) { return static_cast<size_t>(dir[l].size); };

    BspData b;
    b.entities = parseEntities(std::string(reinterpret_cast<const char*>(lump(LUMP_ENTITIES)), size(LUMP_ENTITIES)));
    bool ok = fromBytes(lump(LUMP_PLANES), size(LUMP_PLANES), b.planes) &&
              fromBytes(lump(LUMP_NODES), size(LUMP_NODES), b.nodes) &&
              fromBytes(lump(LUMP_LEAVES), size(LUMP_LEAVES), b.leaves) &&
              fromBytes(lump(LUMP_VERTICES), size(LUMP_VERTICES), b.vertices) &&
              fromBytes(lump(LUMP_INDICES), size(LUMP_INDICES), b.indices) &&
              fromBytes(lump(LUMP_FACES), size(LUMP_FACES), b.faces) &&
              fromBytes(lump(LUMP_LEAFFACES), size(LUMP_LEAFFACES), b.leafFaces) &&
              fromBytes(lump(LUMP_BRUSHES), size(LUMP_BRUSHES), b.brushes) &&
              fromBytes(lump(LUMP_BRUSHSIDES), size(LUMP_BRUSHSIDES), b.brushSides) &&
              fromBytes(lump(LUMP_LEAFBRUSHES), size(LUMP_LEAFBRUSHES), b.leafBrushes) &&
              fromBytes(lump(LUMP_MODELS), size(LUMP_MODELS), b.models);
    if (!ok) return fail("lump size mismatch");
    {
        net::ByteReader m({lump(LUMP_MATERIALS), size(LUMP_MATERIALS)});
        uint32_t n = m.read<uint32_t>();
        for (uint32_t i = 0; i < n && !m.overflowed(); ++i) b.materials.push_back(m.readString());
        if (m.overflowed()) return fail("bad material lump");
    }
    b.visData.assign(lump(LUMP_VISIBILITY), lump(LUMP_VISIBILITY) + size(LUMP_VISIBILITY));
    {
        net::ByteReader l({lump(LUMP_LIGHTMAP), size(LUMP_LIGHTMAP)});
        b.lightmapWidth = l.read<int32_t>();
        b.lightmapHeight = l.read<int32_t>();
        size_t n = static_cast<size_t>(std::max(0, b.lightmapWidth)) * static_cast<size_t>(std::max(0, b.lightmapHeight)) * 3;
        if (l.remaining() < n * 2) return fail("bad lightmap lump");
        b.lightmap.resize(n);
        for (auto& v : b.lightmap) v = l.read<uint16_t>();
    }

    // Validate indices so a corrupt file can't crash the engine.
    for (auto& n : b.nodes) {
        if (n.plane < 0 || static_cast<size_t>(n.plane) >= b.planes.size()) return fail("bad node plane");
        for (int c : n.children) {
            if (c >= 0 ? static_cast<size_t>(c) >= b.nodes.size() : static_cast<size_t>(-1 - c) >= b.leaves.size())
                return fail("bad node child");
        }
    }
    for (auto& f : b.faces)
        if (static_cast<size_t>(f.firstIndex) + f.numIndices > b.indices.size() || f.material >= b.materials.size())
            return fail("bad face");
    for (auto i : b.indices)
        if (i >= b.vertices.size()) return fail("bad index");
    for (auto& l : b.leaves)
        if (static_cast<size_t>(l.firstLeafFace) + l.numLeafFaces > b.leafFaces.size() ||
            static_cast<size_t>(l.firstLeafBrush) + l.numLeafBrushes > b.leafBrushes.size())
            return fail("bad leaf");
    for (auto f : b.leafFaces)
        if (f >= b.faces.size()) return fail("bad leaf face");
    for (auto& m : b.models)
        if (static_cast<size_t>(m.firstFace) + m.numFaces > b.faces.size()) return fail("bad model");
    return b;
}

} // namespace k::bsp
