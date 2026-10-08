#pragma once

#include <cmath>

namespace rts {
enum class MovementType { Walking, Swimming, Amphibious, Flying };
inline bool airborne(MovementType type) { return type == MovementType::Flying; }
struct Cell {
    int x{};
    int y{};
    bool operator==(const Cell&) const = default;
};
inline Cell operator+(Cell a, Cell b) { return {a.x + b.x, a.y + b.y}; }
inline Cell operator-(Cell a, Cell b) { return {a.x - b.x, a.y - b.y}; }

struct Vec2 { float x{}; float y{}; bool operator==(const Vec2&) const = default; };
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline Vec2 center(Cell c) { return {c.x + 0.5f, c.y + 0.5f}; }

// Simulation positions stay in the authored cell grid. Distances use a flat,
// orthonormal ground plane. Scale preserves a one-cell corridor's clearance:
// a body of radius .5 still fits between its walls after projection.
// This metric has no camera/zoom/elevation dependency. Isometry is visual only.
inline constexpr float groundPlaneScale = 1.118033989f;
inline Vec2 groundToPlane(Vec2 v) { return Vec2{v.x - v.y, (v.x + v.y) * .5f} * groundPlaneScale; }
inline Vec2 planeToGround(Vec2 v) { return Vec2{v.x * .5f + v.y, v.y - v.x * .5f} * (1 / groundPlaneScale); }
inline float groundDot(Vec2 a, Vec2 b) {
    // Exact binary coefficients of transpose(A)*A avoid rounding at tangency.
    return 1.5625f * (a.x * b.x + a.y * b.y) - .9375f * (a.x * b.y + a.y * b.x);
}
inline float groundLengthSquared(Vec2 v) { return groundDot(v, v); }
inline float groundLength(Vec2 v) { return std::sqrt(groundLengthSquared(v)); }
// Axis-aligned grid bounds of a circle in the ground plane.
inline float groundRadiusExtent(float radius) { return radius; }

// Logical square cells project to 2:1 diamonds. Simulation never uses pixels.
struct WorldView {
    static constexpr float tileSize = 64.0f;
    static constexpr float levelHeight = 24.0f;
    Vec2 origin{};
    float zoom = 1.0f;

    Vec2 project(Vec2 world, float height = 0.0f) const {
        return origin + Vec2{(world.x - world.y) * tileSize,
            (world.x + world.y) * tileSize * .5f - height * levelHeight} * zoom;
    }
    Vec2 unproject(Vec2 screen, float height = 0.0f) const {
        const auto p = (screen - origin) * (1.0f / zoom);
        const float x = p.x / tileSize, y = (p.y + height * levelHeight) * 2 / tileSize;
        return {(x + y) * .5f, (y - x) * .5f};
    }
};
}
