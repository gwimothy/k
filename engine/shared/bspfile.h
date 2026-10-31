#pragma once
// The compiled map format (.kbsp), shared by the compiler (tools/kbsp), the server
// (entities, collision) and the renderer (faces, visibility, lightmaps).
//
// Like Source's .bsp it is a set of lumps: arrays of plain structs plus a text entity lump.
// Coordinates are engine units, Z up.

#include "shared/kmath.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace k::bsp {

constexpr uint32_t kMagic = 0x5053424B; // "KBSP"
constexpr uint32_t kVersion = 1;

enum Contents : uint32_t {
    CONTENTS_EMPTY = 0,
    CONTENTS_SOLID = 1u << 0,
    CONTENTS_SKY = 1u << 1,       // solid, and the sun/sky is visible through it
    CONTENTS_DETAIL = 1u << 2,    // brush doesn't split the BSP or block visibility
    CONTENTS_PLAYERCLIP = 1u << 3,
    CONTENTS_OUTSIDE = 1u << 4,   // leaf outside the sealed map
};

enum FaceFlags : uint32_t {
    FACE_NODRAW = 1u << 0,
    FACE_SKY = 1u << 1,
};

struct Plane {
    Vec3 normal;
    float dist;
    float distance(const Vec3& p) const { return glm::dot(normal, p) - dist; }
};

// Children >= 0 are node indices; < 0 are leaves: leaf = -1 - child.
struct Node {
    int32_t plane;
    int32_t children[2]; // front, back
    Vec3 mins, maxs;
};

struct Leaf {
    uint32_t contents;
    Vec3 mins, maxs;
    uint32_t firstLeafFace, numLeafFaces;
    uint32_t firstLeafBrush, numLeafBrushes;
    int32_t visOffset; // byte offset into visData, -1 = sees everything
};

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec2 lightmapUV;
};

struct Face {
    uint32_t firstIndex, numIndices; // triangles into `indices`
    uint32_t material;               // index into `materials`
    int32_t plane;
    uint32_t flags;
    int32_t lightmapX, lightmapY, lightmapW, lightmapH; // atlas rect, w = 0 if unlit
};

struct Brush {
    uint32_t firstSide, numSides;
    uint32_t contents;
};

struct BrushSide {
    int32_t plane;
    uint32_t material;
};

// Model 0 is the world. Others are brush entities (entity key "model" "*N").
struct Model {
    Vec3 mins, maxs;
    uint32_t firstFace, numFaces;
    int32_t headNode; // world only for now; -1 for brush entities
};

using Entity = std::map<std::string, std::string>;

struct BspData {
    std::vector<Plane> planes;
    std::vector<Node> nodes;
    std::vector<Leaf> leaves;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Face> faces;
    std::vector<uint32_t> leafFaces;
    std::vector<Brush> brushes;
    std::vector<BrushSide> brushSides;
    std::vector<uint32_t> leafBrushes;
    std::vector<Model> models;
    std::vector<std::string> materials;
    std::vector<uint8_t> visData; // per leaf: zero-run-length compressed bitset over leaves
    int32_t lightmapWidth = 0, lightmapHeight = 0;
    std::vector<uint16_t> lightmap; // RGB half floats, linear
    std::vector<Entity> entities;

    // ---- Queries -------------------------------------------------------------------
    // Leaf index containing a point.
    int findLeaf(const Vec3& p) const;
    // Decompress the PVS row for a leaf into one byte per leaf (1 = potentially visible).
    void decompressVis(int leaf, std::vector<uint8_t>& out) const;
    uint32_t pointContents(const Vec3& p) const;

    // First entity with the given classname, or nullptr.
    const Entity* findEntity(const std::string& classname) const;
};

// Serialization
std::vector<uint8_t> write(const BspData& bsp);
std::optional<BspData> read(const std::vector<uint8_t>& bytes, std::string* error = nullptr);

// Entity lump text: { "key" "value" ... } blocks, one per entity.
std::string writeEntities(const std::vector<Entity>& ents);
std::vector<Entity> parseEntities(const std::string& text);

// PVS compression helpers (zero bytes are run-length encoded as 0, count).
std::vector<uint8_t> compressVis(const std::vector<uint8_t>& bits);
void decompressVisRow(const uint8_t* src, size_t srcAvailable, size_t rowBytes, std::vector<uint8_t>& out);

// Helpers for entity keys
Vec3 entityVec3(const Entity& e, const std::string& key, Vec3 def = Vec3(0));
std::string entityString(const Entity& e, const std::string& key, const std::string& def = {});
float entityFloat(const Entity& e, const std::string& key, float def = 0.0f);

} // namespace k::bsp
