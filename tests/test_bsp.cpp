#include "compiler.h"
#include "shared/bsp_trace.h"
#include "shared/bspfile.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>

using namespace k;

#include "test_room.h"

using k::test::testRoom;

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
    // World faces (model 0) only: brush entities keep all their faces.
    REQUIRE(!b.models.empty());
    for (uint32_t fi = 0; fi < b.models[0].numFaces; ++fi) {
        const auto& f = b.faces[b.models[0].firstFace + fi];
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
