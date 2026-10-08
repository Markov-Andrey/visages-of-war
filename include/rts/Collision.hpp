#pragma once
#include "rts/Types.hpp"
#include <algorithm>

namespace rts {
struct Circle { Vec2 position; float radius; };
inline Cell cellAt(Vec2 p) { return {int(std::floor(p.x)), int(std::floor(p.y))}; }
inline float pointSegmentDistanceSquared(Vec2 p, Vec2 a, Vec2 b) {
    const auto d = b - a;
    const float length = groundLengthSquared(d);
    const auto v = p - a;
    const float t = length > 0 ? std::clamp(groundDot(v, d) / length, 0.0f, 1.0f) : 0;
    return groundLengthSquared(p - (a + d * t));
}
inline float segmentDistanceSquared(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const auto cross = [](Vec2 u, Vec2 v) { return u.x * v.y - u.y * v.x; };
    const auto ab = b - a, cd = d - c;
    const float denominator = cross(ab, cd);
    if (std::abs(denominator) > 1e-8f) {
        const float t = cross(c - a, cd) / denominator, s = cross(c - a, ab) / denominator;
        if (t >= 0 && t <= 1 && s >= 0 && s <= 1) return 0;
    }
    return std::min({pointSegmentDistanceSquared(a, c, d), pointSegmentDistanceSquared(b, c, d),
        pointSegmentDistanceSquared(c, a, b), pointSegmentDistanceSquared(d, a, b)});
}
// Square proximity is the broad phase; the narrow phase tests the entire swept circle.
inline bool sweptCircleIntersects(Vec2 from, Vec2 to, float radius, Circle other) {
    const float sum = radius + other.radius;
    const float extent = groundRadiusExtent(sum);
    if (other.position.x < std::min(from.x, to.x) - extent || other.position.x > std::max(from.x, to.x) + extent ||
        other.position.y < std::min(from.y, to.y) - extent || other.position.y > std::max(from.y, to.y) + extent) return false;
    return pointSegmentDistanceSquared(other.position, from, to) < sum * sum - 1e-6f;
}
inline bool sweptCircleIntersectsCell(Vec2 from, Vec2 to, float radius, Cell cell) {
    const float x = float(cell.x), y = float(cell.y);
    const auto inside = [&](Vec2 p) { return p.x >= x && p.x <= x + 1 && p.y >= y && p.y <= y + 1; };
    if (inside(from) || inside(to)) return true;
    const float distance = std::min({segmentDistanceSquared(from, to, {x, y}, {x + 1, y}),
        segmentDistanceSquared(from, to, {x + 1, y}, {x + 1, y + 1}),
        segmentDistanceSquared(from, to, {x + 1, y + 1}, {x, y + 1}),
        segmentDistanceSquared(from, to, {x, y + 1}, {x, y})});
    return distance < radius * radius - 1e-6f;
}
}
