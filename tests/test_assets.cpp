#include "render/mesh.h"
#include "render/model.h"

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>

using namespace k;

TEST_CASE("MeshData groups indices by material and computes bounds") {
    MeshData m;
    m.addBox({0, 0, 0}, {64, 32, 16}, "dev/a");
    m.addQuad({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, "dev/b");
    CHECK(m.sections.size() == 2);
    CHECK(m.triangleCount() == 14);
    Vec3 lo, hi;
    m.bounds(lo, hi);
    CHECK(lo == Vec3(0, 0, 0));
    CHECK(hi == Vec3(64, 32, 16));
    // Box normals point outward.
    for (auto& v : m.vertices)
        if (v.position.z == 16 && v.normal.z != 0) CHECK(v.normal.z > 0);
}

TEST_CASE("World-aligned UVs repeat every textureWorldSize units") {
    Vec2 uv = worldAlignedUV({256, 512, 0}, {0, 0, 1}, 256.0f);
    CHECK(uv.x == doctest::Approx(1.0f));
    CHECK(uv.y == doctest::Approx(-2.0f));
    uv = worldAlignedUV({0, 128, 64}, {1, 0, 0}, 256.0f);
    CHECK(uv.x == doctest::Approx(0.5f));
    CHECK(uv.y == doctest::Approx(-0.25f));
}

TEST_CASE("glTF loader reads the bundled crate model") {
    std::ifstream f(K_SOURCE_DIR "/base/models/props/crate.glb", std::ios::binary);
    REQUIRE(f);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string error;
    auto mesh = loadGltf(bytes, "crate", &error);
    REQUIRE_MESSAGE(mesh, error);
    CHECK(mesh->vertices.size() == 24);
    CHECK(mesh->triangleCount() == 12);
    REQUIRE(mesh->sections.size() == 1);
    CHECK(mesh->sections[0].material == "models/props/crate");
    Vec3 lo, hi;
    mesh->bounds(lo, hi);
    CHECK(lo.z == doctest::Approx(0.0f));
    CHECK(hi.z == doctest::Approx(32.0f));
}

TEST_CASE("glTF loader rejects garbage") {
    std::vector<uint8_t> junk = {1, 2, 3, 4};
    std::string error;
    CHECK_FALSE(loadGltf(junk, "junk", &error));
    CHECK_FALSE(error.empty());
}
