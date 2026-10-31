#include "entity.h"

#include "core/keyvalues.h"
#include "core/log.h"
#include "game_world.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace k::game {

// ---- Names -----------------------------------------------------------------------------

bool NameLess::operator()(const std::string& a, const std::string& b) const {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

bool nameEquals(const std::string& a, const std::string& b) { return !NameLess{}(a, b) && !NameLess{}(b, a); }

bool nameMatches(const std::string& pattern, const std::string& name) {
    if (!pattern.empty() && pattern.back() == '*') {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        return name.size() >= prefix.size() && nameEquals(name.substr(0, prefix.size()), prefix);
    }
    return nameEquals(pattern, name);
}

bool parseConnection(const std::string& text, Connection& out) {
    std::vector<std::string> parts(1);
    for (char c : text) {
        if (c == ',' || c == '\x1b') parts.emplace_back();
        else parts.back() += c;
    }
    if (parts.size() < 2 || parts[0].empty() || parts[1].empty()) return false;
    out = {};
    out.target = parts[0];
    out.input = parts[1];
    if (parts.size() > 2) out.param = parts[2];
    if (parts.size() > 3 && !parts[3].empty()) out.delay = std::max(0.0f, std::strtof(parts[3].c_str(), nullptr));
    if (parts.size() > 4 && !parts[4].empty()) out.timesToFire = static_cast<int>(std::strtol(parts[4].c_str(), nullptr, 10));
    if (out.timesToFire == 0) out.timesToFire = -1;
    return true;
}

void Output::fire(BaseEntity* activator, BaseEntity* caller, const std::string& value) {
    if (!caller) return;
    for (auto it = connections_.begin(); it != connections_.end();) {
        std::string param = it->param.empty() ? value : it->param;
        caller->world().events().add(*it, param, activator, caller);
        if (it->timesToFire > 0 && --it->timesToFire == 0) it = connections_.erase(it);
        else ++it;
    }
}

// ---- BaseEntity ------------------------------------------------------------------------

bool BaseEntity::keyValue(const std::string& key, const std::string& value) {
    if (nameEquals(key, "targetname")) { targetname = value; return true; }
    if (nameEquals(key, "origin")) { origin = parseVec3(value); return true; }
    if (nameEquals(key, "angles")) {
        Vec3 a = parseVec3(value);
        angles = {a.x, a.y, a.z};
        return true;
    }
    if (nameEquals(key, "angle")) { angles.yaw = std::strtof(value.c_str(), nullptr); return true; }
    if (nameEquals(key, "model")) {
        if (!value.empty() && value[0] == '*') brushModel = std::atoi(value.c_str() + 1);
        else modelPath = value;
        return true;
    }
    // Output connections: "OnSomething" or "OnSomething#2".
    std::string outName = key.substr(0, key.find('#'));
    if (Output* out = findOutput(outName)) {
        Connection c;
        if (parseConnection(value, c)) out->addConnection(c);
        else log::warn("{} '{}': bad output connection \"{}\" = \"{}\"", classname, targetname, key, value);
        return true;
    }
    return false;
}

bool BaseEntity::acceptInput(const std::string& input, BaseEntity* /*activator*/, BaseEntity* /*caller*/,
                             const std::string& /*param*/) {
    if (nameEquals(input, "Kill")) {
        markedForDeletion = true;
        return true;
    }
    if (nameEquals(input, "Enable") || nameEquals(input, "Disable")) return false; // per-class
    return false;
}

Vec3 BaseEntity::absMins() const {
    if (brushModel > 0 && world_ && world_->map() && static_cast<size_t>(brushModel) < world_->map()->models.size())
        return world_->map()->models[static_cast<size_t>(brushModel)].mins + origin;
    return origin;
}

Vec3 BaseEntity::absMaxs() const {
    if (brushModel > 0 && world_ && world_->map() && static_cast<size_t>(brushModel) < world_->map()->models.size())
        return world_->map()->models[static_cast<size_t>(brushModel)].maxs + origin;
    return origin;
}

void BaseEntity::declareOutput(const std::string& name, Output* output) { outputs_[name] = output; }

Output* BaseEntity::findOutput(const std::string& name) {
    auto it = outputs_.find(name);
    return it == outputs_.end() ? nullptr : it->second;
}

// ---- Factory -----------------------------------------------------------------------------

namespace {
std::map<std::string, EntityFactory, NameLess>& factories() {
    static std::map<std::string, EntityFactory, NameLess> f;
    return f;
}
} // namespace

void registerEntityClass(const std::string& classname, EntityFactory factory) { factories()[classname] = std::move(factory); }

std::unique_ptr<BaseEntity> createEntity(const std::string& classname) {
    auto it = factories().find(classname);
    if (it == factories().end()) return nullptr;
    auto e = it->second();
    e->classname = classname;
    return e;
}

std::vector<std::string> registeredEntityClasses() {
    std::vector<std::string> out;
    for (auto& [name, _] : factories()) out.push_back(name);
    return out;
}

} // namespace k::game
