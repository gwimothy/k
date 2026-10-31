#pragma once
// Math conventions (same as Source / Quake, and what TrenchBroom maps use):
//   * Z is up, X is forward, Y is left.
//   * Angles are in degrees: QAngle(pitch, yaw, roll).
//     Positive pitch looks DOWN. Yaw rotates counter-clockwise seen from above.
//   * 1 unit ~= 1 inch; a player is 72 units tall.

#include <glm/glm.hpp>

#include <cmath>

namespace k {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;

// x = pitch, y = yaw, z = roll
struct QAngle {
    float pitch = 0, yaw = 0, roll = 0;
    bool operator==(const QAngle&) const = default;
};

constexpr float kPi = 3.14159265358979323846f;
constexpr float deg2rad(float d) { return d * (kPi / 180.0f); }
constexpr float rad2deg(float r) { return r * (180.0f / kPi); }

// Wrap to (-180, 180].
inline float normalizeAngle(float a) {
    a = std::fmod(a, 360.0f);
    if (a > 180.0f) a -= 360.0f;
    if (a <= -180.0f) a += 360.0f;
    return a;
}

// Shortest-path angle interpolation.
inline float lerpAngle(float from, float to, float t) {
    float delta = normalizeAngle(to - from);
    return from + delta * t;
}

inline QAngle lerpAngles(const QAngle& a, const QAngle& b, float t) {
    return {lerpAngle(a.pitch, b.pitch, t), lerpAngle(a.yaw, b.yaw, t), lerpAngle(a.roll, b.roll, t)};
}

// Source's AngleVectors.
inline void angleVectors(const QAngle& angles, Vec3* forward, Vec3* right = nullptr, Vec3* up = nullptr) {
    float sp = std::sin(deg2rad(angles.pitch)), cp = std::cos(deg2rad(angles.pitch));
    float sy = std::sin(deg2rad(angles.yaw)), cy = std::cos(deg2rad(angles.yaw));
    float sr = std::sin(deg2rad(angles.roll)), cr = std::cos(deg2rad(angles.roll));
    if (forward) *forward = {cp * cy, cp * sy, -sp};
    if (right) *right = {-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
    if (up) *up = {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
}

} // namespace k
