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

// Square screen-aligned terrain; height lifts the surface vertically.
// Sprite perspective is independent of this world-to-screen transform.
struct WorldView {
    static constexpr float tileSize = 64.0f;
    static constexpr float levelHeight = 24.0f;
    Vec2 origin{};
    float zoom = 1.0f;

    Vec2 project(Vec2 world, float height = 0.0f) const {
        return origin + Vec2{world.x * tileSize, world.y * tileSize - height * levelHeight} * zoom;
    }
    Vec2 unproject(Vec2 screen, float height = 0.0f) const {
        const auto p = (screen - origin) * (1.0f / zoom);
        return {p.x / tileSize, (p.y + height * levelHeight) / tileSize};
    }
};
}
