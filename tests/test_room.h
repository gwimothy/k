#pragma once
// A small compiled test map shared by the BSP and movement tests:
// a sealed 512 x 512 x 256 room with a pillar at the center, a 16-unit step at
// x 160..224 / y -200..-136 and a 64-unit block at x -200..-136 / y -200..-136.

#include "compiler.h"
#include "shared/bspfile.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace k::test {

// Compiles a small sealed box room (512 x 512 x 256 inside) with a pillar, once.
inline const bsp::BspData& testRoom() {
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
        map += box(160, -200, 0, 224, -136, 16, "dev/dev_blue");  // a 16-unit step
        map += box(-200, -200, 0, -136, -136, 64, "dev/dev_blue"); // a 64-unit block
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


} // namespace k::test
