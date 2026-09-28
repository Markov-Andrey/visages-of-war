#include "rts/Map.hpp"
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
float Map::surfaceHeight(Cell c, Vec2 world) const {
    const auto& t = at(c);
    float slope = 0;
    if (t.ramp.x == 1) slope = world.x - c.x;
    if (t.ramp.x == -1) slope = c.x + 1.0f - world.x;
    if (t.ramp.y == 1) slope = world.y - c.y;
    if (t.ramp.y == -1) slope = c.y + 1.0f - world.y;
    return t.height + std::clamp(slope, 0.0f, 1.0f);
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