// The built-in test level. Units are inches, Z up; the player starts at the origin.

#include "render/renderer.h"

namespace k {

MeshData buildDevRoom() {
    MeshData m;
    // Main hall: 2048 x 2048, open to the sky (no ceiling) so the skybox shows.
    const Vec3 lo{-1024, -1024, 0}, hi{1024, 1024, 384};
    m.addQuad({lo.x, lo.y, 0}, {hi.x, lo.y, 0}, {hi.x, hi.y, 0}, {lo.x, hi.y, 0}, "dev/dev_floor");
    m.addQuad({hi.x, lo.y, 0}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, 0}, "dev/dev_wall");
    m.addQuad({lo.x, lo.y, 0}, {lo.x, hi.y, 0}, {lo.x, hi.y, hi.z}, {lo.x, lo.y, hi.z}, "dev/dev_wall");
    m.addQuad({lo.x, hi.y, 0}, {hi.x, hi.y, 0}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, "dev/dev_wall");
    m.addQuad({lo.x, lo.y, 0}, {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, lo.y, 0}, "dev/dev_wall");

    // Block of 64-unit cubes (a standard Source crate size).
    m.addBox({256, -64, 0}, {320, 0, 64}, "dev/dev_orange");
    m.addBox({256, 0, 0}, {320, 64, 64}, "dev/dev_orange");
    m.addBox({256, -32, 64}, {320, 32, 128}, "dev/dev_orange");

    // Stairs: 8 steps of 16 units high (Source's max step height is 18).
    for (int i = 0; i < 8; ++i) {
        float x0 = -512.0f + static_cast<float>(i) * 24.0f;
        m.addBox({x0, 256, 0}, {x0 + 24, 448, 16.0f * static_cast<float>(i + 1)}, "dev/dev_blue");
    }
    m.addBox({-320, 256, 0}, {-128, 448, 128}, "dev/dev_blue");

    // Ramp (~27 degrees) leading to a platform.
    m.addRampX({-512, -448, 0}, {-256, -256, 128}, "dev/dev_green");
    m.addBox({-256, -448, 0}, {-64, -256, 128}, "dev/dev_green");

    // Pillars for scale.
    for (float x : {-640.0f, 640.0f})
        for (float y : {-640.0f, 640.0f}) m.addBox({x - 32, y - 32, 0}, {x + 32, y + 32, 384}, "dev/dev_wall");

    // A missing material on purpose, so the error texture is visible somewhere.
    m.addBox({600, 300, 0}, {632, 332, 32}, "dev/this_material_does_not_exist");
    return m;
}

std::vector<std::pair<std::string, Vec3>> devRoomProps() {
    return {
        {"models/props/crate.glb", {160, -160, 0}},
        {"models/props/crate.glb", {200, -160, 0}},
        {"models/props/crate.glb", {180, -160, 32}},
        {"models/props/crate.glb", {160, 160, 0}},
    };
}

} // namespace k
