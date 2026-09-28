#pragma once
#include "rts/Map.hpp"
#include <span>

namespace rts {
struct Path { std::vector<Cell> cells; int cost{}; };
std::optional<Path> findPath(const Map& map, Cell start, std::span<const Cell> goals, std::span<const Cell> occupied = {}, MovementType movement = MovementType::Walking);
std::optional<Path> findPath(const Map& map, Cell start, Cell goal, MovementType movement = MovementType::Walking);
// Reachable adjacent tiles are the targets, never the occupied object tile.
std::vector<Cell> perimeter(const Map& map, Cell topLeft, int width, int height, MovementType movement = MovementType::Walking);
}
