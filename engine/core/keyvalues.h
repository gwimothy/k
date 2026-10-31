#pragma once
// KeyValues: Source's nested text format, used for materials, and later for entity data,
// game info and sound scripts.
//
//   "LitGeneric"
//   {
//       "$basetexture"  "dev/dev_orange"
//       $color          "[1 0.5 0.5]"     // quotes are optional for single words
//       "proxies" { "spin" { "rate" "90" } }
//   }

#include "shared/kmath.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace k {

class KeyValues {
public:
    KeyValues() = default;
    KeyValues(std::string name, std::string value) : name_(std::move(name)), value_(std::move(value)) {}

    // Parses text into a root node whose children are the top-level entries.
    // On a syntax error returns nullopt and sets *error (if given) to "line N: message".
    static std::optional<KeyValues> parse(std::string_view text, std::string* error = nullptr);

    const std::string& name() const { return name_; }
    const std::string& value() const { return value_; }
    bool isSection() const { return section_; }
    const std::vector<KeyValues>& children() const { return children_; }

    // Case-insensitive lookup of a direct child. Returns the first match or nullptr.
    const KeyValues* find(std::string_view key) const;

    std::string getString(std::string_view key, std::string_view def = {}) const;
    float getFloat(std::string_view key, float def = 0.0f) const;
    int getInt(std::string_view key, int def = 0) const;
    bool getBool(std::string_view key, bool def = false) const;
    // Accepts "[r g b]", "{r g b}" or "r g b".
    Vec3 getVec3(std::string_view key, Vec3 def = Vec3(0)) const;

    // Building
    KeyValues& add(std::string name, std::string value);
    KeyValues& addSection(std::string name);

    std::string serialize(int indent = 0) const;

private:
    std::string name_;
    std::string value_;
    bool section_ = false;
    std::vector<KeyValues> children_;
};

// Parse "1 2 3", "[1 2 3]" or "{1 2 3}" into a vector. Missing components are left at def.
Vec3 parseVec3(std::string_view text, Vec3 def = Vec3(0));

} // namespace k
