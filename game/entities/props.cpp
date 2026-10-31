// Physics props.

#include "core/filesystem.h"
#include "core/keyvalues.h"
#include "core/log.h"
#include "entity.h"
#include "game_world.h"
#include "render/model.h"

#include <cstdlib>

namespace k::game {
namespace {

// A model simulated by the physics engine: falls, tumbles, can be pushed, picked up (+use)
// and thrown (+attack while held).
class PropPhysics : public BaseEntity {
public:
    bool keyValue(const std::string& k, const std::string& v) override {
        if (nameEquals(k, "massScale")) { massScale_ = std::max(0.01f, std::strtof(v.c_str(), nullptr)); return true; }
        return BaseEntity::keyValue(k, v);
    }
    void spawn() override {
        networked = true;
        auto* physics = world().physics();
        if (!physics) return;
        std::vector<Vec3> points;
        if (auto bytes = fs::readBinary(modelPath)) {
            std::string error;
            if (auto mesh = loadGltf(*bytes, modelPath, &error))
                for (auto& v : mesh->vertices) points.push_back(v.position);
            else
                log::warn("prop_physics: {}: {}", modelPath, error);
        } else {
            log::warn("prop_physics: missing model '{}'", modelPath);
        }
        if (points.empty()) points = {{-8, -8, 0}, {8, 8, 16}, {-8, 8, 0}, {8, -8, 16}};
        physicsBody = physics->addDynamicHull(points, origin, angles, massScale_);
        physicsDriven = physicsBody.valid();
    }
    bool acceptInput(const std::string& in, BaseEntity* activator, BaseEntity* caller, const std::string& p) override {
        if (nameEquals(in, "Wake")) { world().physics()->wake(physicsBody); return true; }
        if (nameEquals(in, "ApplyImpulse")) {
            world().physics()->wake(physicsBody);
            world().physics()->addImpulse(physicsBody, parseVec3(p) * world().physics()->mass(physicsBody));
            return true;
        }
        return BaseEntity::acceptInput(in, activator, caller, p);
    }

private:
    float massScale_ = 1.0f;
};
LINK_ENTITY_TO_CLASS(prop_physics, PropPhysics)

} // namespace

void linkEntities_props() {}

} // namespace k::game
