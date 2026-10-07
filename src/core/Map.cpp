#include "rts/Map.hpp"
#include "rts/Collision.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>

namespace rts {
Map::Map(int width, int height) : width_(width), height_(height) {
    if (width < 1 || height < 1 || width > 512 || height > 512)
        throw std::invalid_argument("Map dimensions must be in [1, 512]");
    tiles_.resize(static_cast<size_t>(width) * height);
    occupancy_.resize(tiles_.size());
    visionBlockers_.resize(tiles_.size());
}
void Map::rebuildVisionBlockers(std::span<const EnvironmentObject> objects) {
    std::fill(visionBlockers_.begin(), visionBlockers_.end(), false);
    for (const auto& object : objects)
        for (int y = 0; y < object.height; ++y) for (int x = 0; x < object.width; ++x) {
            const Cell c = object.origin + Cell{x, y};
            if (contains(c) && object.occludes(x, y)) visionBlockers_[static_cast<size_t>(c.y) * width_ + c.x] = true;
        }
}
bool Map::blocksVision(Cell c) const {
    return !contains(c) || visionBlockers_[static_cast<size_t>(c.y) * width_ + c.x];
}
bool Map::contains(Cell c) const { return c.x >= 0 && c.y >= 0 && c.x < width_ && c.y < height_; }
bool Map::walkable(Cell c, MovementType movement) const {
    if (!contains(c)) return false;
    if (airborne(movement)) return true;
    if (at(c).blocked || occupancy(c) != 0) return false;
    const auto surface = at(c).surface;
    if (movement == MovementType::Walking) return surface != Surface::DeepWater;
    if (movement == MovementType::Swimming) return surface != Surface::Land;
    return true;
}
unsigned Map::occupancy(Cell c) const {
    if (!contains(c)) throw std::out_of_range("Occupancy outside map");
    return occupancy_[static_cast<size_t>(c.y) * width_ + c.x];
}
void Map::occupy(Cell c) {
    if (!contains(c)) throw std::out_of_range("Occupancy outside map");
    ++occupancy_[static_cast<size_t>(c.y) * width_ + c.x];
}
void Map::release(Cell c) {
    if (occupancy(c) == 0) throw std::logic_error("Unbalanced occupancy release");
    --occupancy_[static_cast<size_t>(c.y) * width_ + c.x];
}
Tile& Map::at(Cell c) {
    if (!contains(c)) throw std::out_of_range("Tile outside map");
    return tiles_[static_cast<size_t>(c.y) * width_ + c.x];
}
const Tile& Map::at(Cell c) const {
    if (!contains(c)) throw std::out_of_range("Tile outside map");
    return tiles_[static_cast<size_t>(c.y) * width_ + c.x];
}
bool Map::cardinalStep(Cell from, Cell to, MovementType movement) const {
    if (!walkable(from, movement) || !walkable(to, movement)) return false;
    const Cell delta = to - from;
    if (std::abs(delta.x) + std::abs(delta.y) != 1) return false;
    const int dh = at(to).height - at(from).height;
    if (dh == 0) {
        // Ramp side entrances would otherwise make units jump onto the slope.
        const auto a = at(from).ramp;
        const auto b = at(to).ramp;
        // Adjacent lanes of a wide ramp share a continuous lateral surface.
        // Along the slope, equal-height ramps would still form a discontinuity.
        if (a != Cell{} && a == b && delta.x * a.x + delta.y * a.y == 0) return true;
        return (a == Cell{} || delta == Cell{-a.x, -a.y}) &&
               (b == Cell{} || delta == b);
    }
    if (dh == 1) return at(from).ramp == delta;
    if (dh == -1) return at(to).ramp == Cell{-delta.x, -delta.y};
    return false;
}
bool Map::canStep(Cell from, Cell to, MovementType movement) const {
    const Cell d = to - from;
    if (std::abs(d.x) > 1 || std::abs(d.y) > 1 || d == Cell{}) return false;
    if (airborne(movement)) return contains(from) && contains(to);
    if (d.x == 0 || d.y == 0) return cardinalStep(from, to, movement);
    const Cell sideX{from.x + d.x, from.y};
    const Cell sideY{from.x, from.y + d.y};
    // Both sides of a diagonal must be clear; never cut a corner or cliff.
    return cardinalStep(from, sideX, movement) && cardinalStep(from, sideY, movement) &&
           cardinalStep(sideX, to, movement) && cardinalStep(sideY, to, movement) &&
           at(from).height == at(to).height && at(from).ramp == Cell{} && at(to).ramp == Cell{};
}
bool Map::canTraverse(Vec2 from, Vec2 to, float radius, MovementType movement) const {
    if (!std::isfinite(radius) || radius <= 0 || !std::isfinite(from.x) || !std::isfinite(from.y) ||
        !std::isfinite(to.x) || !std::isfinite(to.y)) return false;
    for (Vec2 p : {from, to}) if (p.x < radius || p.y < radius || p.x > width_ - radius || p.y > height_ - radius) return false;
    if (airborne(movement)) return true;
    // Trace every centre crossing through the same ramp/corner rules as grid navigation.
    Cell current = cellAt(from);
    const Cell finish = cellAt(to);
    if (!walkable(current, movement)) return false;
    const Vec2 delta = to - from;
    const Cell direction{delta.x > 0 ? 1 : -1, delta.y > 0 ? 1 : -1};
    while (current != finish) {
        const float tx = current.x == finish.x ? 2.0f : (current.x + (direction.x > 0 ? 1.0f : 0.0f) - from.x) / delta.x;
        const float ty = current.y == finish.y ? 2.0f : (current.y + (direction.y > 0 ? 1.0f : 0.0f) - from.y) / delta.y;
        Cell next = current;
        if (tx <= ty + 1e-6f) next.x += direction.x;
        if (ty <= tx + 1e-6f) next.y += direction.y;
        if (!canStep(current, next, movement)) return false;
        current = next;
    }
    const int left = std::max(0, int(std::floor(std::min(from.x, to.x) - radius)));
    const int right = std::min(width_ - 1, int(std::floor(std::max(from.x, to.x) + radius)));
    const int top = std::max(0, int(std::floor(std::min(from.y, to.y) - radius)));
    const int bottom = std::min(height_ - 1, int(std::floor(std::max(from.y, to.y) + radius)));
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        const Cell c{x, y};
        if (!walkable(c, movement)) {
            if (sweptCircleIntersectsCell(from, to, radius, c)) return false;
            continue;
        }
        // Impassable terrain edges remain walls, including ramp sides and shore cliffs.
        for (Cell d : {Cell{1, 0}, Cell{0, 1}}) if (!canStep(c, c + d, movement)) {
            const Vec2 a{float(x + d.x), float(y + d.y)};
            const Vec2 b = a + Vec2{float(d.y), float(d.x)};
            if (segmentDistanceSquared(from, to, a, b) < radius * radius - 1e-6f) return false;
        }
    }
    return true;
}
float Map::surfaceHeight(Cell c, Vec2 world) const {
    const auto& t = at(c);
    float slope = 0;
    if (t.ramp.x == 1) slope = world.x - c.x;
    if (t.ramp.x == -1) slope = c.x + 1.0f - world.x;
    if (t.ramp.y == 1) slope = world.y - c.y;
    if (t.ramp.y == -1) slope = c.y + 1.0f - world.y;
    return t.height + std::clamp(slope, 0.0f, 1.0f);
}
float Map::waterDepth(Cell c, Vec2 world) const {
    const auto& current = at(c);
    if (current.surface == Surface::Land) return 0;
    const int left = int(std::floor(world.x - .5f)), top = int(std::floor(world.y - .5f));
    const auto smooth = [](float t) { return t * t * (3 - 2 * t); };
    const float tx = smooth(world.x - .5f - left), ty = smooth(world.y - .5f - top);
    const auto depth = [&](Cell sample) {
        // Extend edge cells, but never blend separate pools across a cliff.
        sample.x = std::clamp(sample.x, 0, width_ - 1);
        sample.y = std::clamp(sample.y, 0, height_ - 1);
        const auto& t = at(sample);
        if (t.height != current.height || t.surface == Surface::Land) return 0.0f;
        return t.surface == Surface::DeepWater ? 1.8f : .8f;
    };
    const float a = std::lerp(depth({left, top}), depth({left + 1, top}), tx);
    const float b = std::lerp(depth({left, top + 1}), depth({left + 1, top + 1}), tx);
    float shore = 1;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        const Cell neighbor = c + Cell{x, y};
        if (!contains(neighbor) || at(neighbor).surface != Surface::Land || at(neighbor).height != current.height) continue;
        const float dx = world.x - std::clamp(world.x, float(neighbor.x), neighbor.x + 1.0f);
        const float dy = world.y - std::clamp(world.y, float(neighbor.y), neighbor.y + 1.0f);
        const float distance = std::hypot(dx, dy);
        shore = std::min(shore, smooth(std::clamp(distance, 0.0f, 1.0f)));
    }
    return std::lerp(a, b, ty) * shore;
}
float Map::bedHeight(Cell c, Vec2 world) const {
    return surfaceHeight(c, world) - waterDepth(c, world);
}
float Map::movementHeight(Vec2 world, MovementType movement) const {
    if (airborne(movement)) return 5.0f;
    const Cell c{int(std::floor(world.x)), int(std::floor(world.y))};
    const float surface = surfaceHeight(c, world);
    // Swimmers float; walkers stand on the bed. Amphibians float in deeper water.
    const float depth = waterDepth(c, world);
    return surface - (movement == MovementType::Walking ? depth : std::min(depth, .28f));
}
std::array<Vec2, 4> Map::surfaceCorners(Cell c, const WorldView& view) const {
    const std::array<Vec2, 4> world{{{float(c.x), float(c.y)}, {c.x + 1.0f, float(c.y)},
        {c.x + 1.0f, c.y + 1.0f}, {float(c.x), c.y + 1.0f}}};
    std::array<Vec2, 4> result;
    for (size_t i = 0; i < world.size(); ++i) result[i] = view.project(world[i], surfaceHeight(c, world[i]));
    return result;
}
std::optional<Cell> Map::pick(Vec2 screen, const WorldView& view) const {
    const auto inside = [&](const std::array<Vec2, 4>& corners) {
        bool positive = false, negative = false;
        float area = 0;
        for (size_t i = 0; i < corners.size(); ++i) {
            const auto a = corners[i], b = corners[(i + 1) % corners.size()];
            const float cross = (b.x - a.x) * (screen.y - a.y) - (b.y - a.y) * (screen.x - a.x);
            positive |= cross > .01f; negative |= cross < -.01f;
            area += a.x * b.y - a.y * b.x;
        }
        return std::abs(area) > .01f && !(positive && negative);
    };
    // Reverse the renderer's row order. Elevated surfaces can overlap lower rows.
    for (int y = height_ - 1; y >= 0; --y) for (int x = width_ - 1; x >= 0; --x) {
        const Cell c{x, y};
        const auto top = surfaceCorners(c, view);
        if (inside(top)) return c;
        // The front cliff is visible geometry, not a selectable tile behind it.
        const Cell neighbor{x, y + 1};
        const std::array<Vec2, 2> world{{{x + 1.0f, y + 1.0f}, {float(x), y + 1.0f}}};
        std::array<Vec2, 4> face{top[2], top[3], {}, {}};
        for (size_t i = 0; i < 2; ++i) {
            const float below = contains(neighbor) ? surfaceHeight(neighbor, world[i]) : at(c).height - .65f;
            face[3 - i] = view.project(world[i], std::min(below, surfaceHeight(c, world[i])));
        }
        if (inside(face)) return std::nullopt;
    }
    return std::nullopt;
}

void Map::clearOccupancy() { std::fill(occupancy_.begin(), occupancy_.end(), 0); }
std::optional<Vec2> Map::pickPosition(Vec2 screen, const WorldView& view) const {
    const auto c = pick(screen, view);
    if (!c) return std::nullopt;
    const auto corners = surfaceCorners(*c, view);
    const auto u = corners[1] - corners[0], v = corners[3] - corners[0], p = screen - corners[0];
    const float determinant = u.x * v.y - u.y * v.x;
    if (std::abs(determinant) < .0001f) return std::nullopt;
    const float a = (p.x * v.y - p.y * v.x) / determinant, b = (u.x * p.y - u.y * p.x) / determinant;
    return Vec2{c->x + std::clamp(a, 0.0f, .99999f), c->y + std::clamp(b, 0.0f, .99999f)};
}
}
