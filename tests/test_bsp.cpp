#include "compiler.h"
#include "shared/bsp_trace.h"
#include "shared/bspfile.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>

using namespace k;

namespace {

// Compiles a small sealed box room (512 x 512 x 256 inside) with a pillar, once.
const bsp::BspData& testRoom() {
    static bsp::BspData data = [] {
        std::random_device rd;
        auto dir = std::filesystem::temp_directory_path() / ("k_bsp_" + std::to_string(rd()));
        std::filesystem::create_directories(dir);
        auto box = [](int x0, int y0, int z0, int x1, int y1, int z1, const char* tex) {
            char b[1024];
            std::snprintf(b, sizeof(b),
                          "{\n( %d %d %d ) ( %d %d %d ) ( %d %d %d ) %s 0 0 0 1 1\n"  // -x
                          "( %d %d %d ) ( %d %d %d ) ( %d %d %d ) %s 0 0 0 1 1\n"     // +x
                          "( %d %d %d ) ( %d %d %d ) ( %d %d %d ) %s 0 0 0 1 1\n"     // -y
                          "( %d %d %d ) ( %d %d %d ) ( %d %d %d ) %s 0 0 0 1 1\n"     // +y
                          "( %d %d %d ) ( %d %d %d ) ( %d %d %d ) %s 0 0 0 1 1\n"     // -z
                          "( %d %d %d ) ( %d %d %d ) ( %d %d %d ) %s 0 0 0 1 1\n}\n", // +z
                          // Each face: p0, p1, p2 with (p0 - p1) x (p2 - p1) pointing out of the brush.
                          x0, y0 + 1, z0, x0, y0, z0 + 1, x0, y0, z0, tex,
                          x1, y0, z0 + 1, x1, y0 + 1, z0, x1, y0, z0, tex,
                          x0, y0, z0 + 1, x0 + 1, y0, z0, x0, y0, z0, tex,
                          x0 + 1, y1, z0, x0, y1, z0 + 1, x0, y1, z0, tex,
                          x0 + 1, y0, z0, x0, y0 + 1, z0, x0, y0, z0, tex,
                          x0, y0 + 1, z1, x0 + 1, y0, z1, x0, y0, z1, tex);
            return std::string(b);
        };
        std::string map = "{\n\"classname\" \"worldspawn\"\n";
        map += box(-272, -272, -16, 272, 272, 0, "dev/dev_floor");
        map += box(-272, -272, 256, 272, 272, 272, "tools/toolsskybox");
        map += box(-272, -272, 0, -256, 272, 256, "dev/dev_wall");
        map += box(256, -272, 0, 272, 272, 256, "dev/dev_wall");
        map += box(-256, -272, 0, 256, -256, 256, "dev/dev_wall");
        map += box(-256, 256, 0, 256, 272, 256, "dev/dev_wall");
        map += box(-32, -32, 0, 32, 32, 256, "dev/dev_orange"); // pillar
        map += "}\n{\n\"classname\" \"info_player_start\"\n\"origin\" \"128 0 0\"\n}\n";
        map += "{\n\"classname\" \"light_environment\"\n\"pitch\" \"-60\"\n\"angles\" \"0 0 0\"\n}\n";
        map += "{\n\"classname\" \"light\"\n\"origin\" \"-128 -128 128\"\n}\n";
        std::ofstream(dir / "room.map") << map;

        kbsp::Compiler c;
        c.opt.input = dir / "room.map";
        c.opt.output = dir / "room.kbsp";
        c.opt.gameDir = K_SOURCE_DIR "/base";
        c.opt.threads = 2;
        REQUIRE(c.loadMap());
        c.buildBrushes();
        c.buildTree();
        c.makePortals();
        REQUIRE(c.floodFill());
        c.fillOutside();
        c.makeFaces();
        c.assignLeafBrushes();
        c.emitBsp();
        c.computeVis();
        c.computeLighting();
        REQUIRE(c.writeOutput());

        std::ifstream f(dir / "room.kbsp", std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::string error;
        auto parsed = bsp::read(bytes, &error);
        REQUIRE_MESSAGE(parsed, error);
        std::filesystem::remove_all(dir);
        return std::move(*parsed);
    }();
    return data;
}

} // namespace

TEST_CASE("kbsp: plane points follow the Quake convention (normals point out of the brush)") {
    auto p = kbsp::planeFromPoints({0, 0, 64}, {0, 1, 64}, {1, 0, 64}); // a top face
    CHECK(p.normal.z == doctest::Approx(1.0));
    CHECK(p.dist == doctest::Approx(64.0));
}

TEST_CASE("kbsp: winding split keeps area") {
    kbsp::PlaneD floor{{0, 0, 1}, 0};
    kbsp::Winding w = {{-64, -64, 0}, {64, -64, 0}, {64, 64, 0}, {-64, 64, 0}};
    kbsp::Winding f, b;
    kbsp::splitWinding(w, kbsp::PlaneD{{1, 0, 0}, 16}, 0.1, f, b);
    CHECK(kbsp::area(f) + kbsp::area(b) == doctest::Approx(kbsp::area(w)));
    CHECK(kbsp::classify(w, floor) == kbsp::Side::On);
}

TEST_CASE("kbsp: compiled room has the right contents") {
    const auto& b = testRoom();
    CHECK(b.pointContents({128, 0, 64}) == bsp::CONTENTS_EMPTY);     // player start
    CHECK((b.pointContents({0, 0, 64}) & bsp::CONTENTS_SOLID) != 0);        // pillar
    CHECK((b.pointContents({264, 0, 64}) & bsp::CONTENTS_SOLID) != 0);      // wall
    CHECK((b.pointContents({0, 0, 264}) & bsp::CONTENTS_SKY) != 0);         // sky brush
    CHECK((b.pointContents({1000, 1000, 1000}) & bsp::CONTENTS_SOLID) != 0); // outside the map is filled
    REQUIRE(b.findEntity("info_player_start"));
}

TEST_CASE("kbsp: faces are only generated on the inside") {
    const auto& b = testRoom();
    // Floor, 4 walls (sky is nodraw), and the pillar's 4 sides: no outward faces, no pillar caps.
    for (auto& f : b.faces) {
        Vec3 c(0);
        uint32_t n = 0;
        for (uint32_t i = 0; i < f.numIndices; ++i, ++n) c += b.vertices[b.indices[f.firstIndex + i]].position;
        c /= static_cast<float>(n);
        Vec3 normal = b.planes[static_cast<size_t>(f.plane)].normal;
        // Every face must look into empty space.
        CHECK(b.pointContents(c + normal * 2.0f) == bsp::CONTENTS_EMPTY);
    }
    CHECK(b.faces.size() >= 9);
}

TEST_CASE("bsp: line traces") {
    const auto& b = testRoom();
    auto tr = bsp::traceLine(b, {128, 0, 64}, {500, 0, 64}); // into the east wall at x=256
    CHECK(tr.fraction < 1.0f);
    CHECK(tr.endPos.x == doctest::Approx(256.0f).epsilon(0.01));
    CHECK(tr.normal.x == doctest::Approx(-1.0f));
    CHECK_FALSE(tr.startSolid);

    auto up = bsp::traceLine(b, {128, 0, 64}, {128, 0, 1000});
    CHECK((up.contents & bsp::CONTENTS_SKY) != 0);

    auto clear = bsp::traceLine(b, {128, 0, 64}, {128, 100, 64});
    CHECK(clear.fraction == 1.0f);

    auto inside = bsp::traceLine(b, {0, 0, 64}, {128, 0, 64});
    CHECK(inside.startSolid);
}

TEST_CASE("bsp: lightmaps and vis are present") {
    const auto& b = testRoom();
    CHECK(b.lightmapWidth > 0);
    CHECK(b.lightmap.size() == static_cast<size_t>(b.lightmapWidth * b.lightmapHeight * 3));
    int leaf = b.findLeaf({128, 0, 64});
    REQUIRE(leaf >= 0);
    std::vector<uint8_t> vis;
    b.decompressVis(leaf, vis);
    CHECK(vis[static_cast<size_t>(leaf)] == 1);
    for (auto& f : b.faces) CHECK(f.lightmapW > 0);
}

TEST_CASE("bsp: write/read round trip and corruption is rejected") {
    const auto& b = testRoom();
    auto bytes = bsp::write(b);
    auto again = bsp::read(bytes);
    REQUIRE(again);
    CHECK(again->faces.size() == b.faces.size());
    CHECK(again->entities.size() == b.entities.size());
    bytes.resize(bytes.size() / 2); // truncated file: lumps point past the end
    std::string error;
    CHECK_FALSE(bsp::read(bytes, &error));
    std::vector<uint8_t> junk(8, 0);
    CHECK_FALSE(bsp::read(junk, &error));
}

TEST_CASE("bsp: vis compression round trip") {
    std::vector<uint8_t> bits = {0xFF, 0, 0, 0, 0x10, 0, 0x01};
    auto packed = bsp::compressVis(bits);
    std::vector<uint8_t> out;
    bsp::decompressVisRow(packed.data(), packed.size(), bits.size(), out);
    CHECK(out == bits);
}

TEST_CASE("entity lump parsing") {
    auto ents = bsp::parseEntities("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"light\"\n\"origin\" \"1 2 3\"\n}\n");
    REQUIRE(ents.size() == 2);
    CHECK(bsp::entityVec3(ents[1], "origin") == Vec3(1, 2, 3));
}
