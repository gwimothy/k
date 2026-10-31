#pragma once
// kbsp: compiles a .map into a .kbsp.
//
// Pipeline (each stage in its own file):
//   brushes.cpp  map brushes -> convex brushes with planes and side polygons
//   tree.cpp     structural brushes -> solid-leaf BSP tree
//   portals.cpp  portals between leaves, flood fill from entities (leak check, outside removal)
//   faces.cpp    visible brush faces filtered into leaves, collision brushes per leaf, output
//   vis.cpp      potentially visible set per leaf
//   light.cpp    lightmaps from point lights, sun and sky
//
// Every stage only reads/writes the Compiler struct below.

#include "map_file.h"
#include "shared/bspfile.h"
#include "winding.h"

#include <filesystem>
#include <string>
#include <vector>

namespace kbsp {

struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::filesystem::path gameDir; // for texture sizes
    bool vis = true;
    bool light = true;
    bool verbose = false;
    int threads = 0; // 0 = hardware concurrency
    double luxelSize = 16.0;
};

struct TexInfo {
    int side = -1;       // index into map side data (for texture coordinates)
    uint32_t material;   // index into Compiler::materials
    int texWidth = 256, texHeight = 256;
    uint32_t flags = 0;  // bsp::FaceFlags
};

struct BrushSide {
    int plane;        // index into Compiler::planes
    Winding winding;
    int texInfo;      // -1 for sides created by splitting
    bool tested = false; // already used as a splitter on this path
};

struct Brush {
    std::vector<BrushSide> sides;
    uint32_t contents = 0;
    dvec3 mins{0}, maxs{0};
    int original = -1; // index into Compiler::brushes
    int entity = 0;
    void computeBounds();
};

struct Portal;

struct TreeNode {
    bool leaf = false;
    int plane = -1;
    int children[2] = {-1, -1};
    uint32_t contents = 0;
    dvec3 mins{0}, maxs{0};
    std::vector<int> portals;
    int occupied = 0;   // flood fill distance + 1, 0 = not reached
    int parentPortalFrom = -1; // for leak tracing
    std::vector<int> faces;    // output face indices (leaves)
    std::vector<int> brushes;  // collision brush indices (leaves)
    int outputIndex = -1;      // node or leaf index in the BspData
};

struct Portal {
    PlaneD plane;      // node plane; nodes[0] is in front of it
    Winding winding;
    int nodes[2];
    bool alive = true;
};

struct OutFace {
    Winding winding;
    int texInfo;
    int plane;
    int entity = 0;
    int bspFace = -1;
};

struct Compiler {
    Options opt;
    std::vector<MapEntity> entities;
    std::vector<MapSide> mapSides;  // flat list referenced by TexInfo::side
    std::vector<PlaneD> planes;     // pairs: i and i^1 are opposites
    std::vector<TexInfo> texInfos;
    std::vector<std::string> materials;
    std::vector<Brush> brushes;     // every brush of every entity, unsplit
    std::vector<TreeNode> nodes;
    std::vector<Portal> portals;
    std::vector<OutFace> faces;
    int root = -1;
    int outsideNode = -1;
    bool leaked = false;
    dvec3 worldMins{0}, worldMaxs{0};
    k::bsp::BspData bsp;

    // Planes
    int findPlane(const dvec3& normal, double dist);
    // Materials
    uint32_t findMaterial(const std::string& name);

    // Stages
    bool loadMap();
    void buildBrushes();
    void buildTree();
    void makePortals();
    bool floodFill();      // returns false on leak
    void fillOutside();
    void makeFaces();
    void assignLeafBrushes();
    void emitBsp();        // tree, faces, brushes -> bsp
    void computeVis();
    void computeLighting();
    bool writeOutput();

    int leafForPoint(const dvec3& p) const;
};

void info(const char* fmt, ...);
void warn(const char* fmt, ...);
std::string fmtPoint(const dvec3& p);

} // namespace kbsp
