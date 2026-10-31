#pragma once
// Convex polygons ("windings") in double precision, the basic building block of the compiler.

#include <glm/glm.hpp>

#include <vector>

namespace kbsp {

using dvec3 = glm::dvec3;

struct PlaneD {
    dvec3 normal{0};
    double dist = 0;
    double distance(const dvec3& p) const { return glm::dot(normal, p) - dist; }
    PlaneD flipped() const { return {-normal, -dist}; }
};

using Winding = std::vector<dvec3>;

constexpr double kOnEpsilon = 0.1;     // points this close to a plane count as on it
constexpr double kWorldExtent = 65536; // larger than any map

enum class Side { Front, Back, On, Cross };

// A huge square lying on the plane.
Winding baseWinding(const PlaneD& plane);

// Split by a plane. Either output may come back empty.
void splitWinding(const Winding& w, const PlaneD& plane, double epsilon, Winding& front, Winding& back);
// Keep only the part in front of the plane.
Winding clipFront(const Winding& w, const PlaneD& plane, double epsilon = kOnEpsilon);

Side classify(const Winding& w, const PlaneD& plane, double epsilon = kOnEpsilon);

double area(const Winding& w);
dvec3 center(const Winding& w);
bool isTiny(const Winding& w);
PlaneD planeFromPoints(const dvec3& a, const dvec3& b, const dvec3& c);

} // namespace kbsp
