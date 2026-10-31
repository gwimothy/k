// kbsp - compile a TrenchBroom .map into a .kbsp
//
//   kbsp [options] maps/src/mymap.map
//     -game <dir>    game directory with materials/ (default: base)
//     -o <file>      output path (default: <game>/maps/<name>.kbsp)
//     -novis         skip visibility (everything visible)
//     -nolight       skip lightmaps (fullbright)
//     -luxel <n>     lightmap luxel size in units (default 16)
//     -threads <n>   worker threads (default: all cores)

#include "compiler.h"
#include "core/time.h"

#include <cstdio>
#include <cstring>
#include <fstream>

using namespace kbsp;


int main(int argc, char** argv) {
    Options opt;
    opt.gameDir = "base";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "-game") opt.gameDir = next();
        else if (a == "-o") opt.output = next();
        else if (a == "-novis") opt.vis = false;
        else if (a == "-nolight") opt.light = false;
        else if (a == "-luxel") opt.luxelSize = std::max(1.0, std::atof(next().c_str()));
        else if (a == "-threads") opt.threads = std::atoi(next().c_str());
        else if (a == "-v") opt.verbose = true;
        else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        } else opt.input = a;
    }
    if (opt.input.empty()) {
        std::fprintf(stderr, "usage: kbsp [-game dir] [-o out.kbsp] [-novis] [-nolight] [-luxel n] [-threads n] map.map\n");
        return 2;
    }
    if (opt.output.empty()) opt.output = opt.gameDir / "maps" / (opt.input.stem().string() + ".kbsp");

    double start = k::platformTime();
    Compiler c;
    c.opt = opt;
    info("kbsp: %s", opt.input.string().c_str());
    if (!c.loadMap()) return 1;
    c.buildBrushes();
    c.buildTree();
    c.makePortals();
    bool sealed = c.floodFill();
    if (sealed) c.fillOutside();
    c.makeFaces();
    c.assignLeafBrushes();
    c.emitBsp();
    if (opt.vis && sealed) c.computeVis();
    else info("vis: skipped%s", sealed ? "" : " (map leaks)");
    if (opt.light) c.computeLighting();
    else info("lighting: skipped");
    if (!c.writeOutput()) return 1;
    info("done in %.2f s", k::platformTime() - start);
    return 0;
}
