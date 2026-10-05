#pragma once
#include "rts/Map.hpp"
#include "rts/Collision.hpp"
#include <span>

namespace rts {
struct Path { std::vector<Cell> cells; int cost{}; std::optional<Vec2> destination; };
std::optional<Path> findPath(const Map& map, Cell start, std::span<const Cell> goals, std::span<const Cell> occupied = {}, MovementType movement = MovementType::Walking);
std::optional<Path> findPath(const Map& map, Cell start, Cell goal, MovementType movement = MovementType::Walking);
std::optional<Path> findUnitPath(const Map& map, Vec2 start, std::span<const Cell> goals,
    std::span<const Circle> obstacles, float radius, MovementType movement);
std::optional<Path> findUnitPathTo(const Map& map, Vec2 start, Vec2 goal,
    std::span<const Circle> obstacles, float radius, MovementType movement);
struct RouteField {
    int width{};
    MovementType movement = MovementType::Walking;
    std::vector<int> costs;
    std::vector<Cell> next;
};
RouteField makeRouteField(const Map& map, std::span<const Cell> goals, MovementType movement);
std::optional<Path> fieldPath(const Map& map, const RouteField& field, Vec2 start, Cell goal, float radius);
// Reachable adjacent tiles are the targets, never the occupied object tile.
std::vector<Cell> perimeter(const Map& map, Cell topLeft, int width, int height, MovementType movement = MovementType::Walking);
}
