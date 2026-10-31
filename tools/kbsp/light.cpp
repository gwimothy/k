// Lightmaps.
//
// Every drawable face gets a grid of luxels (one per `luxelSize` units, 16 by default like
// Source). Each luxel gathers:
//   * point lights ("light" entities) with inverse-square falloff and shadow rays
//   * the sun (light_environment) if a ray toward it reaches a sky brush
//   * sky ambient: cosine-weighted rays that reach the sky
// Results are linear HDR, packed into one atlas stored as half floats.
// Radiosity bounces are a later improvement.

#include "compiler.h"
#include "core/keyvalues.h"
#include "shared/bsp_trace.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace kbsp {
namespace {

using k::Vec3;

struct PointLight {
    Vec3 origin;
    Vec3 color; // linear, already scaled by brightness
};

struct Sun {
    bool present = false;
    Vec3 toSun{0, 0, 1};
    Vec3 color{0};
    Vec3 ambient{0};
};

Vec3 srgbToLinear(Vec3 c) { return glm::pow(glm::clamp(c, 0.0f, 1.0f), Vec3(2.2f)); }

// "_light" "r g b brightness" (Source) or "light" "300" (Quake).
Vec3 parseLightColor(const MapEntity& e, const char* key, float defBrightness) {
    std::string v = e.get(key);
    float r = 255, g = 255, b = 255, i = defBrightness;
    if (!v.empty()) {
        int n = std::sscanf(v.c_str(), "%f %f %f %f", &r, &g, &b, &i);
        if (n == 1) { i = r; r = g = b = 255; }
        else if (n == 3) i = defBrightness;
    } else if (!e.get("light").empty()) {
        i = std::strtof(e.get("light").c_str(), nullptr);
    }
    return srgbToLinear(Vec3(r, g, b) / 255.0f) * (i / 255.0f);
}

struct FaceLight {
    int face = -1;            // compiler face
    int axisS = 0, axisT = 1, axisN = 2;
    double lux = 16;
    double smin = 0, tmin = 0;
    int w = 0, h = 0;         // luxels (without border)
    std::vector<Vec3> luxels; // w*h
    int atlasX = 0, atlasY = 0;
};

// Points on a hemisphere around +Z, cosine weighted (Hammersley sequence).
std::vector<Vec3> hemisphereSamples(int n) {
    std::vector<Vec3> out;
    for (int i = 0; i < n; ++i) {
        uint32_t bits = static_cast<uint32_t>(i);
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        float v = static_cast<float>(bits) * 2.3283064365386963e-10f;
        float r = std::sqrt(u), phi = 2.0f * 3.14159265f * v;
        out.push_back({r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u))});
    }
    return out;
}

} // namespace

void Compiler::computeLighting() {
    std::vector<PointLight> lights;
    Sun sun;
    float minLight = std::strtof(entities[0].get("_minlight", "0").c_str(), nullptr) / 255.0f;
    for (auto& e : entities) {
        std::string cls = e.get("classname");
        if (cls == "light") {
            lights.push_back({Vec3(k::parseVec3(e.get("origin"))), parseLightColor(e, "_light", 200)});
        } else if (cls == "light_environment") {
            sun.present = true;
            sun.color = parseLightColor(e, "_light", 200);
            sun.ambient = parseLightColor(e, "_ambient", 40);
            Vec3 ang = k::parseVec3(e.get("angles", "0 0 0"));
            float pitch = e.get("pitch").empty() ? ang.x : std::strtof(e.get("pitch").c_str(), nullptr);
            k::Vec3 fwd;
            // Source convention: pitch -90 means the sun is straight overhead.
            k::angleVectors({pitch, ang.y, 0}, &fwd);
            sun.toSun = glm::normalize(fwd);
        }
    }
    info("lighting: %zu point lights%s", lights.size(), sun.present ? ", sun" : "");

    std::vector<FaceLight> fls;
    for (size_t i = 0; i < faces.size(); ++i) {
        if (faces[i].bspFace < 0) continue;
        FaceLight fl;
        fl.face = static_cast<int>(i);
        const dvec3 n = planes[static_cast<size_t>(faces[i].plane)].normal;
        dvec3 a = glm::abs(n);
        if (a.z >= a.x && a.z >= a.y) { fl.axisS = 0; fl.axisT = 1; fl.axisN = 2; }
        else if (a.x >= a.y) { fl.axisS = 1; fl.axisT = 2; fl.axisN = 0; }
        else { fl.axisS = 0; fl.axisT = 2; fl.axisN = 1; }
        fl.lux = opt.luxelSize;
        double smin = 1e30, smax = -1e30, tmin = 1e30, tmax = -1e30;
        for (auto& p : faces[i].winding) {
            smin = std::min(smin, p[fl.axisS]); smax = std::max(smax, p[fl.axisS]);
            tmin = std::min(tmin, p[fl.axisT]); tmax = std::max(tmax, p[fl.axisT]);
        }
        for (;;) {
            fl.smin = std::floor(smin / fl.lux);
            fl.tmin = std::floor(tmin / fl.lux);
            fl.w = static_cast<int>(std::ceil(smax / fl.lux) - fl.smin) + 1;
            fl.h = static_cast<int>(std::ceil(tmax / fl.lux) - fl.tmin) + 1;
            if (fl.w <= 254 && fl.h <= 254) break;
            fl.lux *= 2;
        }
        fls.push_back(std::move(fl));
    }

    auto samples = hemisphereSamples(48);
    std::atomic<size_t> next{0};
    auto worker = [&] {
        for (size_t fi; (fi = next++) < fls.size();) {
            FaceLight& fl = fls[fi];
            const OutFace& f = faces[static_cast<size_t>(fl.face)];
            const PlaneD& plane = planes[static_cast<size_t>(f.plane)];
            const Vec3 n = Vec3(plane.normal);
            // Tangent frame for hemisphere samples.
            Vec3 t = std::fabs(n.z) < 0.9f ? glm::normalize(glm::cross(Vec3(0, 0, 1), n)) : Vec3(1, 0, 0);
            Vec3 b = glm::cross(n, t);
            dvec3 centroid = center(f.winding);
            // Inward edge planes to keep samples on the face.
            std::vector<PlaneD> edges;
            for (size_t e = 0; e < f.winding.size(); ++e) {
                dvec3 p0 = f.winding[e], p1 = f.winding[(e + 1) % f.winding.size()];
                dvec3 en = glm::cross(plane.normal, p1 - p0);
                double len = glm::length(en);
                if (len < 1e-9) continue;
                en /= len;
                edges.push_back({en, glm::dot(en, p0)});
            }
            fl.luxels.assign(static_cast<size_t>(fl.w * fl.h), Vec3(0));
            for (int y = 0; y < fl.h; ++y)
                for (int x = 0; x < fl.w; ++x) {
                    dvec3 p(0);
                    p[fl.axisS] = (fl.smin + x) * fl.lux;
                    p[fl.axisT] = (fl.tmin + y) * fl.lux;
                    p[fl.axisN] = (plane.dist - plane.normal[fl.axisS] * p[fl.axisS] -
                                   plane.normal[fl.axisT] * p[fl.axisT]) / plane.normal[fl.axisN];
                    // Pull the sample onto the polygon.
                    for (int iter = 0; iter < 2; ++iter)
                        for (auto& e : edges) {
                            double d = e.distance(p);
                            if (d < 0.5) p += e.normal * (0.5 - d);
                        }
                    Vec3 pos = Vec3(p) + n * 1.0f;
                    // If that's inside a wall, slide toward the face center.
                    for (int step = 0; step < 8 && (bsp.pointContents(pos) & k::bsp::CONTENTS_SOLID); ++step)
                        pos = glm::mix(pos, Vec3(centroid) + n * 1.0f, 0.25f);

                    Vec3 sum(minLight);
                    for (auto& l : lights) {
                        Vec3 d = l.origin - pos;
                        float dist = glm::length(d);
                        if (dist < 1e-3f) continue;
                        float ndl = glm::dot(n, d / dist);
                        if (ndl <= 0) continue;
                        if (k::bsp::traceLine(bsp, pos, l.origin).fraction < 1.0f) continue;
                        float atten = (100.0f * 100.0f) / std::max(dist * dist, 256.0f);
                        sum += l.color * (ndl * atten);
                    }
                    if (sun.present) {
                        float ndl = glm::dot(n, sun.toSun);
                        if (ndl > 0) {
                            auto tr = k::bsp::traceLine(bsp, pos, pos + sun.toSun * 32768.0f);
                            if (tr.fraction < 1.0f && (tr.contents & k::bsp::CONTENTS_SKY)) sum += sun.color * ndl;
                        }
                        int hits = 0;
                        for (auto& s : samples) {
                            Vec3 dir = t * s.x + b * s.y + n * s.z;
                            auto tr = k::bsp::traceLine(bsp, pos, pos + dir * 32768.0f);
                            if (tr.fraction < 1.0f && (tr.contents & k::bsp::CONTENTS_SKY)) ++hits;
                        }
                        sum += sun.ambient * (static_cast<float>(hits) / static_cast<float>(samples.size()));
                    }
                    fl.luxels[static_cast<size_t>(y * fl.w + x)] = sum;
                }
        }
    };
    unsigned nthreads = opt.threads > 0 ? static_cast<unsigned>(opt.threads) : std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < nthreads; ++i) pool.emplace_back(worker);
    for (auto& th : pool) th.join();

    // Shelf-pack (with a 1-luxel border around each face) into the smallest square-ish atlas.
    std::vector<size_t> order(fls.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return fls[a].h > fls[b].h; });
    int atlasW = 256;
    int atlasH = 0;
    for (;;) {
        int x = 0, y = 0, shelf = 0;
        bool fits = true;
        for (size_t idx : order) {
            FaceLight& fl = fls[idx];
            int w = fl.w + 2, h = fl.h + 2;
            if (w > atlasW) { fits = false; break; }
            if (x + w > atlasW) { x = 0; y += shelf; shelf = 0; }
            fl.atlasX = x;
            fl.atlasY = y;
            x += w;
            shelf = std::max(shelf, h);
        }
        atlasH = y + shelf;
        if (fits && atlasH <= atlasW) break;
        atlasW *= 2;
    }
    atlasH = std::max(atlasH, 1);

    bsp.lightmapWidth = atlasW;
    bsp.lightmapHeight = atlasH;
    bsp.lightmap.assign(static_cast<size_t>(atlasW) * static_cast<size_t>(atlasH) * 3, glm::packHalf1x16(0.0f));
    for (auto& fl : fls) {
        for (int y = -1; y <= fl.h; ++y)
            for (int x = -1; x <= fl.w; ++x) {
                int sx = std::clamp(x, 0, fl.w - 1), sy = std::clamp(y, 0, fl.h - 1);
                Vec3 c = fl.luxels[static_cast<size_t>(sy * fl.w + sx)];
                size_t o = (static_cast<size_t>(fl.atlasY + y + 1) * static_cast<size_t>(atlasW) +
                            static_cast<size_t>(fl.atlasX + x + 1)) * 3;
                for (int ch = 0; ch < 3; ++ch) bsp.lightmap[o + static_cast<size_t>(ch)] = glm::packHalf1x16(c[ch]);
            }
        const OutFace& f = faces[static_cast<size_t>(fl.face)];
        k::bsp::Face& bf = bsp.faces[static_cast<size_t>(f.bspFace)];
        bf.lightmapX = fl.atlasX;
        bf.lightmapY = fl.atlasY;
        bf.lightmapW = fl.w + 2;
        bf.lightmapH = fl.h + 2;
        // Vertex lightmap coordinates: luxel (0,0) center is at atlas pixel (x+1, y+1) center.
        uint32_t first = bsp.indices[bf.firstIndex];
        for (size_t v = 0; v < f.winding.size(); ++v) {
            auto& vert = bsp.vertices[first + v];
            double s = vert.position[fl.axisS] / fl.lux - fl.smin;
            double tt = vert.position[fl.axisT] / fl.lux - fl.tmin;
            vert.lightmapUV = k::Vec2(static_cast<float>((fl.atlasX + 1 + s + 0.5) / atlasW),
                                      static_cast<float>((fl.atlasY + 1 + tt + 0.5) / atlasH));
        }
    }
    size_t luxels = 0;
    for (auto& fl : fls) luxels += fl.luxels.size();
    info("lightmaps: %zu luxels in a %dx%d atlas", luxels, atlasW, atlasH);
}

} // namespace kbsp
