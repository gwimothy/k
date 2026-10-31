#include "map_file.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace kbsp {
namespace {

class Tokenizer {
public:
    explicit Tokenizer(const std::string& t) : text_(t) {}

    bool next(std::string& tok) {
        skip();
        if (pos_ >= text_.size()) return false;
        tok.clear();
        char c = text_[pos_];
        if (c == '"') {
            ++pos_;
            while (pos_ < text_.size() && text_[pos_] != '"') tok += text_[pos_++];
            ++pos_;
            quoted_ = true;
            return true;
        }
        quoted_ = false;
        if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']') {
            tok = c;
            ++pos_;
            return true;
        }
        while (pos_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[pos_])) && text_[pos_] != '(' &&
               text_[pos_] != ')' && text_[pos_] != '[' && text_[pos_] != ']')
            tok += text_[pos_++];
        return true;
    }

    bool peek(std::string& tok) {
        size_t p = pos_;
        int l = line_;
        bool r = next(tok);
        pos_ = p;
        line_ = l;
        return r;
    }

    bool quoted() const { return quoted_; }
    int line() const { return line_; }

private:
    void skip() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == '\n') { ++line_; ++pos_; }
            else if (std::isspace(static_cast<unsigned char>(c))) ++pos_;
            else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
            } else break;
        }
    }

    const std::string& text_;
    size_t pos_ = 0;
    int line_ = 1;
    bool quoted_ = false;
};

// Quake's texture axis table: for each base axis, (normal, s axis, t axis).
const dvec3 kBaseAxis[18] = {
    {0, 0, 1}, {1, 0, 0}, {0, -1, 0},  // floor
    {0, 0, -1}, {1, 0, 0}, {0, -1, 0}, // ceiling
    {1, 0, 0}, {0, 1, 0}, {0, 0, -1},  // west wall
    {-1, 0, 0}, {0, 1, 0}, {0, 0, -1}, // east wall
    {0, 1, 0}, {1, 0, 0}, {0, 0, -1},  // south wall
    {0, -1, 0}, {1, 0, 0}, {0, 0, -1}, // north wall
};

} // namespace

std::optional<std::vector<MapEntity>> parseMap(const std::string& text, std::string* error) {
    Tokenizer tz(text);
    std::vector<MapEntity> ents;
    std::string tok;
    auto fail = [&](const std::string& msg) -> std::optional<std::vector<MapEntity>> {
        if (error) *error = "line " + std::to_string(tz.line()) + ": " + msg;
        return std::nullopt;
    };
    auto number = [&](double& out) {
        if (!tz.next(tok)) return false;
        char* end = nullptr;
        out = std::strtod(tok.c_str(), &end);
        return end != tok.c_str();
    };
    auto expect = [&](const char* s) { return tz.next(tok) && tok == s; };

    while (tz.next(tok)) {
        if (tok != "{") return fail("expected '{' to start an entity, got '" + tok + "'");
        MapEntity ent;
        for (;;) {
            if (!tz.next(tok)) return fail("unexpected end of file in entity");
            if (tok == "}") break;
            if (tok == "{") {
                // Brush
                MapBrush brush;
                brush.line = tz.line();
                for (;;) {
                    if (!tz.peek(tok)) return fail("unexpected end of file in brush");
                    if (tok == "}") { tz.next(tok); break; }
                    if (tok != "(") {
                        // Patches and other primitives aren't supported: skip the block.
                        return fail("unsupported brush primitive '" + tok + "' (only brushes are supported)");
                    }
                    MapSide side;
                    for (auto& p : side.points) {
                        if (!expect("(") || !number(p.x) || !number(p.y) || !number(p.z) || !expect(")"))
                            return fail("bad plane point");
                    }
                    if (!tz.next(side.texture)) return fail("missing texture name");
                    if (!tz.peek(tok)) return fail("unexpected end of file");
                    if (tok == "[") {
                        side.valve220 = true;
                        if (!expect("[") || !number(side.uAxis.x) || !number(side.uAxis.y) || !number(side.uAxis.z) ||
                            !number(side.uOffset) || !expect("]"))
                            return fail("bad Valve 220 U axis");
                        if (!expect("[") || !number(side.vAxis.x) || !number(side.vAxis.y) || !number(side.vAxis.z) ||
                            !number(side.vOffset) || !expect("]"))
                            return fail("bad Valve 220 V axis");
                        if (!number(side.rotation) || !number(side.uScale) || !number(side.vScale))
                            return fail("bad texture rotation/scale");
                    } else {
                        if (!number(side.uOffset) || !number(side.vOffset) || !number(side.rotation) ||
                            !number(side.uScale) || !number(side.vScale))
                            return fail("bad texture offset/rotation/scale");
                    }
                    // Quake 2/3 style trailing content/surface flags: skip numbers until the next side.
                    while (tz.peek(tok) && tok != "(" && tok != "}") tz.next(tok);
                    if (side.uScale == 0) side.uScale = 1;
                    if (side.vScale == 0) side.vScale = 1;
                    brush.sides.push_back(side);
                }
                ent.brushes.push_back(std::move(brush));
            } else {
                std::string key = tok, value;
                if (!tz.next(value)) return fail("missing value for key '" + key + "'");
                ent.keys[key] = value;
            }
        }
        ents.push_back(std::move(ent));
    }
    return ents;
}

glm::dvec2 textureCoords(const MapSide& side, const dvec3& normal, const dvec3& p) {
    if (side.valve220) {
        return {glm::dot(p, side.uAxis) / side.uScale + side.uOffset, glm::dot(p, side.vAxis) / side.vScale + side.vOffset};
    }
    // Standard Quake format: project along the closest base axis, then rotate and scale.
    int best = 0;
    double bestDot = -1;
    for (int i = 0; i < 6; ++i) {
        double d = glm::dot(normal, kBaseAxis[i * 3]);
        if (d > bestDot + 1e-6) { bestDot = d; best = i; }
    }
    dvec3 s = kBaseAxis[best * 3 + 1], t = kBaseAxis[best * 3 + 2];
    double ang = side.rotation * 3.14159265358979323846 / 180.0;
    double sinv = std::sin(ang), cosv = std::cos(ang);
    int sv = s.x != 0 ? 0 : s.y != 0 ? 1 : 2;
    int tv = t.x != 0 ? 0 : t.y != 0 ? 1 : 2;
    dvec3 rs = s, rt = t;
    rs[sv] = cosv * s[sv] - sinv * s[tv];
    rs[tv] = sinv * s[sv] + cosv * s[tv];
    rt[sv] = cosv * t[sv] - sinv * t[tv];
    rt[tv] = sinv * t[sv] + cosv * t[tv];
    return {glm::dot(p, rs) / side.uScale + side.uOffset, glm::dot(p, rt) / side.vScale + side.vOffset};
}

} // namespace kbsp
