// The built-in test level. Units are inches, Z up; the player starts at the origin.

#include "render/renderer.h"

namespace k {

MeshData buildDevRoom() {
    MeshData m;
    const Vec3 floorC{0.42f, 0.42f, 0.44f};
    const Vec3 wallC{0.70f, 0.70f, 0.68f};
    const Vec3 ceilC{0.55f, 0.55f, 0.58f};
    const Vec3 orange{0.90f, 0.52f, 0.18f};
    const Vec3 blue{0.25f, 0.45f, 0.80f};
    const Vec3 green{0.35f, 0.65f, 0.35f};

    // Main hall: 2048 x 2048, 384 tall.
    m.addRoom({-1024, -1024, 0}, {1024, 1024, 384}, floorC, wallC, ceilC);

    // Crates (64 units = a standard Source crate).
    m.addBox({256, -64, 0}, {320, 0, 64}, orange);
    m.addBox({256, 0, 0}, {320, 64, 64}, orange);
    m.addBox({256, -32, 64}, {320, 32, 128}, orange);

    // Stairs: 8 steps of 16 units high (max step height in Source is 18).
    for (int i = 0; i < 8; ++i) {
        float x0 = -512.0f + static_cast<float>(i) * 24.0f;
        m.addBox({x0, 256, 0}, {x0 + 24, 448, 16.0f * static_cast<float>(i + 1)}, blue);
    }
    m.addBox({-320, 256, 0}, {-128, 448, 128}, blue); // landing

    // Ramp (~27 degrees) leading to a platform.
    m.addRampX({-512, -448, 0}, {-256, -256, 128}, green);
    m.addBox({-256, -448, 0}, {-64, -256, 128}, green);

    // Pillars for scale.
    for (float x : {-640.0f, 640.0f})
        for (float y : {-640.0f, 640.0f}) m.addBox({x - 32, y - 32, 0}, {x + 32, y + 32, 384}, wallC);

    return m;
}

} // namespace k
