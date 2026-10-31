#include "winding.h"

#include <cmath>

namespace kbsp {

Winding baseWinding(const PlaneD& plane) {
    // Pick the major axis, build an "up" vector perpendicular to the normal.
    dvec3 n = plane.normal;
    dvec3 a = glm::abs(n);
    dvec3 up = (a.z >= a.x && a.z >= a.y) ? dvec3(1, 0, 0) : dvec3(0, 0, 1);
    up = glm::normalize(up - n * glm::dot(up, n));
    dvec3 right = glm::cross(up, n);
    dvec3 org = n * plane.dist;
    up *= kWorldExtent;
    right *= kWorldExtent;
    return {org - right + up, org + right + up, org + right - up, org - right - up};
}

void splitWinding(const Winding& w, const PlaneD& plane, double eps, Winding& front, Winding& back) {
    front.clear();
    back.clear();
    size_t n = w.size();
    if (n == 0) return;
    std::vector<double> dists(n);
    std::vector<int> sides(n);
    int counts[3] = {0, 0, 0}; // front, back, on
    for (size_t i = 0; i < n; ++i) {
        dists[i] = plane.distance(w[i]);
        sides[i] = dists[i] > eps ? 0 : dists[i] < -eps ? 1 : 2;
        counts[sides[i]]++;
    }
    if (counts[0] == 0 && counts[1] == 0) return; // on the plane: caller decides
    if (counts[0] == 0) { back = w; return; }
    if (counts[1] == 0) { front = w; return; }

    for (size_t i = 0; i < n; ++i) {
        const dvec3& p1 = w[i];
        if (sides[i] == 2) {
            front.push_back(p1);
            back.push_back(p1);
            continue;
        }
        (sides[i] == 0 ? front : back).push_back(p1);
        size_t j = (i + 1) % n;
        if (sides[j] == 2 || sides[j] == sides[i]) continue;
        const dvec3& p2 = w[j];
        double t = dists[i] / (dists[i] - dists[j]);
        dvec3 mid;
        for (int k = 0; k < 3; ++k) {
            // Snap exactly onto axial planes to avoid drift.
            if (plane.normal[k] == 1) mid[k] = plane.dist;
            else if (plane.normal[k] == -1) mid[k] = -plane.dist;
            else mid[k] = p1[k] + t * (p2[k] - p1[k]);
        }
        front.push_back(mid);
        back.push_back(mid);
    }
    if (front.size() < 3) front.clear();
    if (back.size() < 3) back.clear();
}

Winding clipFront(const Winding& w, const PlaneD& plane, double eps) {
    Winding f, b;
    splitWinding(w, plane, eps, f, b);
    if (f.empty() && b.empty()) return w; // on plane: keep
    return f;
}

Side classify(const Winding& w, const PlaneD& plane, double eps) {
    bool front = false, back = false;
    for (auto& p : w) {
        double d = plane.distance(p);
        if (d > eps) front = true;
        else if (d < -eps) back = true;
    }
    if (front && back) return Side::Cross;
    if (front) return Side::Front;
    if (back) return Side::Back;
    return Side::On;
}

double area(const Winding& w) {
    double a = 0;
    for (size_t i = 2; i < w.size(); ++i) a += glm::length(glm::cross(w[i - 1] - w[0], w[i] - w[0]));
    return a * 0.5;
}

dvec3 center(const Winding& w) {
    dvec3 c(0);
    for (auto& p : w) c += p;
    return w.empty() ? c : c / static_cast<double>(w.size());
}

bool isTiny(const Winding& w) {
    // Fewer than three edges longer than a small threshold (q3map's heuristic).
    int edges = 0;
    for (size_t i = 0; i < w.size(); ++i) {
        if (glm::length(w[(i + 1) % w.size()] - w[i]) > 0.2) {
            if (++edges == 3) return false;
        }
    }
    return true;
}

PlaneD planeFromPoints(const dvec3& a, const dvec3& b, const dvec3& c) {
    // Quake .map convention: normal = (a - b) x (c - b), pointing out of the brush.
    dvec3 n = glm::cross(a - b, c - b);
    double len = glm::length(n);
    if (len < 1e-9) return {};
    n /= len;
    // Snap nearly-axial normals.
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(n[k] - 1) < 1e-9) n = dvec3(0), n[k] = 1;
        else if (std::fabs(n[k] + 1) < 1e-9) n = dvec3(0), n[k] = -1;
    }
    double d = glm::dot(n, b);
    if (std::fabs(d - std::round(d)) < 1e-6) d = std::round(d);
    return {n, d};
}

} // namespace kbsp
