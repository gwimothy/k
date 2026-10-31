#include "compiler.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>

namespace kbsp {

void info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

void warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "WARNING: ");
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fprintf(stderr, "\n");
}

std::string fmtPoint(const dvec3& p) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "(%.0f %.0f %.0f)", p.x, p.y, p.z);
    return buf;
}

void Brush::computeBounds() {
    mins = dvec3(1e30);
    maxs = dvec3(-1e30);
    for (auto& s : sides)
        for (auto& p : s.winding) {
            mins = glm::min(mins, p);
            maxs = glm::max(maxs, p);
        }
}

int Compiler::findPlane(const dvec3& normalIn, double dist) {
    dvec3 normal = normalIn;
    // Canonical orientation: the even plane of each pair has a positive major axis.
    bool flip = false;
    dvec3 a = glm::abs(normal);
    int major = (a.x >= a.y && a.x >= a.z) ? 0 : (a.y >= a.z ? 1 : 2);
    if (normal[major] < 0) flip = true;
    dvec3 cn = flip ? -normal : normal;
    double cd = flip ? -dist : dist;

    for (size_t i = 0; i < planes.size(); i += 2) {
        const PlaneD& p = planes[i];
        if (std::fabs(p.dist - cd) < 0.01 && glm::length(p.normal - cn) < 1e-5)
            return static_cast<int>(i) + (flip ? 1 : 0);
    }
    planes.push_back({cn, cd});
    planes.push_back({-cn, -cd});
    return static_cast<int>(planes.size()) - 2 + (flip ? 1 : 0);
}

uint32_t Compiler::findMaterial(const std::string& nameIn) {
    std::string name = nameIn;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    std::replace(name.begin(), name.end(), '\\', '/');
    for (size_t i = 0; i < materials.size(); ++i)
        if (materials[i] == name) return static_cast<uint32_t>(i);
    materials.push_back(name);
    return static_cast<uint32_t>(materials.size() - 1);
}

namespace {

// Texture size from a PNG header; 256x256 if the file is missing or not a PNG.
void textureSize(const std::filesystem::path& gameDir, const std::string& material, int& w, int& h) {
    w = h = 256;
    std::ifstream f(gameDir / "materials" / (material + ".png"), std::ios::binary);
    unsigned char hdr[24];
    if (!f.read(reinterpret_cast<char*>(hdr), 24)) return;
    if (hdr[0] != 0x89 || hdr[1] != 'P') return;
    w = (hdr[16] << 24) | (hdr[17] << 16) | (hdr[18] << 8) | hdr[19];
    h = (hdr[20] << 24) | (hdr[21] << 16) | (hdr[22] << 8) | hdr[23];
    if (w <= 0 || h <= 0) w = h = 256;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

} // namespace

bool Compiler::loadMap() {
    std::ifstream f(opt.input, std::ios::binary);
    if (!f) {
        warn("can't open %s", opt.input.string().c_str());
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string error;
    auto ents = parseMap(text, &error);
    if (!ents) {
        warn("%s: %s", opt.input.string().c_str(), error.c_str());
        return false;
    }
    if (ents->empty() || (*ents)[0].get("classname") != "worldspawn") {
        warn("the first entity must be worldspawn");
        return false;
    }
    entities = std::move(*ents);
    return true;
}

void Compiler::buildBrushes() {
    worldMins = dvec3(1e30);
    worldMaxs = dvec3(-1e30);
    int dropped = 0;
    for (size_t e = 0; e < entities.size(); ++e) {
        const MapEntity& ent = entities[e];
        std::string cls = ent.get("classname");
        bool detail = cls == "func_detail";
        for (const MapBrush& mb : ent.brushes) {
            Brush b;
            b.entity = static_cast<int>(e);
            bool anySky = false, allClip = true;
            for (const MapSide& ms : mb.sides) {
                PlaneD p = planeFromPoints(ms.points[0], ms.points[1], ms.points[2]);
                if (glm::length(p.normal) < 0.5) continue; // degenerate
                int planeIdx = findPlane(p.normal, p.dist);
                // Duplicate planes on one brush: ignore the repeat.
                bool dup = false;
                for (auto& s : b.sides) dup |= s.plane == planeIdx;
                if (dup) continue;

                std::string tex = ms.texture;
                std::transform(tex.begin(), tex.end(), tex.begin(), [](unsigned char c) { return std::tolower(c); });
                TexInfo ti;
                ti.side = static_cast<int>(mapSides.size());
                mapSides.push_back(ms);
                ti.material = findMaterial(tex);
                textureSize(opt.gameDir, materials[ti.material], ti.texWidth, ti.texHeight);
                if (startsWith(tex, "tools/toolsskybox") || tex == "sky") {
                    ti.flags |= k::bsp::FACE_SKY | k::bsp::FACE_NODRAW;
                    anySky = true;
                }
                if (startsWith(tex, "tools/toolsnodraw") || startsWith(tex, "tools/toolsclip") ||
                    startsWith(tex, "tools/toolsplayerclip") || startsWith(tex, "tools/toolstrigger") ||
                    startsWith(tex, "tools/toolsorigin"))
                    ti.flags |= k::bsp::FACE_NODRAW;
                if (!startsWith(tex, "tools/toolsplayerclip") && !startsWith(tex, "tools/toolsclip")) allClip = false;
                texInfos.push_back(ti);
                b.sides.push_back({planeIdx, {}, static_cast<int>(texInfos.size()) - 1});
            }

            // Side polygons: each side's plane clipped by every other side (keep the inside).
            for (size_t i = 0; i < b.sides.size(); ++i) {
                Winding w = baseWinding(planes[static_cast<size_t>(b.sides[i].plane)]);
                for (size_t j = 0; j < b.sides.size() && !w.empty(); ++j) {
                    if (i == j) continue;
                    w = clipFront(w, planes[static_cast<size_t>(b.sides[j].plane ^ 1)], 0.0);
                }
                b.sides[i].winding = std::move(w);
            }
            std::erase_if(b.sides, [](const BrushSide& s) { return s.winding.size() < 3; });
            if (b.sides.size() < 4) {
                warn("entity %zu brush at line %d is degenerate, removed", e, mb.line);
                ++dropped;
                continue;
            }
            b.computeBounds();
            if (glm::any(glm::greaterThan(glm::abs(b.mins), dvec3(kWorldExtent * 0.5))) ||
                glm::any(glm::greaterThan(glm::abs(b.maxs), dvec3(kWorldExtent * 0.5)))) {
                warn("entity %zu brush at line %d is unbounded, removed", e, mb.line);
                ++dropped;
                continue;
            }

            if (allClip) b.contents = k::bsp::CONTENTS_PLAYERCLIP;
            else b.contents = k::bsp::CONTENTS_SOLID | (anySky ? k::bsp::CONTENTS_SKY : 0u);
            if (detail) b.contents |= k::bsp::CONTENTS_DETAIL;
            b.original = static_cast<int>(brushes.size());
            if (e == 0 || detail) {
                worldMins = glm::min(worldMins, b.mins);
                worldMaxs = glm::max(worldMaxs, b.maxs);
            }
            brushes.push_back(std::move(b));
        }
    }
    if (brushes.empty()) worldMins = worldMaxs = dvec3(0);
    info("%zu brushes, %zu planes, %zu materials%s", brushes.size(), planes.size() / 2, materials.size(),
         dropped ? " (some brushes dropped, see warnings)" : "");
}

} // namespace kbsp
