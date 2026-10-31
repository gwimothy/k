#pragma once
// Reads Quake-style .map files as written by TrenchBroom, in both the standard
// format and the Valve 220 format (explicit texture axes).

#include "winding.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kbsp {

struct MapSide {
    dvec3 points[3];
    std::string texture;
    bool valve220 = false;
    dvec3 uAxis{0}, vAxis{0}; // Valve 220 only
    double uOffset = 0, vOffset = 0, rotation = 0, uScale = 1, vScale = 1;
};

struct MapBrush {
    std::vector<MapSide> sides;
    int line = 0; // for error messages
};

struct MapEntity {
    std::map<std::string, std::string> keys;
    std::vector<MapBrush> brushes;
    std::string get(const std::string& key, const std::string& def = {}) const {
        auto it = keys.find(key);
        return it == keys.end() ? def : it->second;
    }
};

std::optional<std::vector<MapEntity>> parseMap(const std::string& text, std::string* error);

// Texture coordinates (in texels) for a point on a side, following the map format's rules.
// Divide by the texture size to get UVs.
glm::dvec2 textureCoords(const MapSide& side, const dvec3& normal, const dvec3& point);

} // namespace kbsp
