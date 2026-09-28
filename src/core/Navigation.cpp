#include "rts/Navigation.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <queue>

namespace rts {
std::optional<Path> findPath(const Map& map, Cell start, std::span<const Cell> goals, std::span<const Cell> occupied, MovementType movement) {
    if (!map.walkable(start, movement) || goals.empty()) return std::nullopt;
    std::vector<bool> blocked(static_cast<size_t>(map.width()) * map.height());
    for (Cell c : occupied) if (map.contains(c) && c != start) blocked[static_cast<size_t>(c.y) * map.width() + c.x] = true;
    const auto free = [&](Cell c) { return map.walkable(c, movement) && !blocked[static_cast<size_t>(c.y) * map.width() + c.x]; };
    std::vector<Cell> validGoals;
    for (Cell goal : goals) if (free(goal)) validGoals.push_back(goal);
    if (validGoals.empty()) return std::nullopt;
    const auto index = [&](Cell c) { return c.y * map.width() + c.x; };
    const auto cell = [&](int i) { return Cell{i % map.width(), i / map.width()}; };
    const auto unoccupied = [&](Cell c) { return !blocked[index(c)]; };
    const auto heuristic = [&](Cell c) {
        int best = std::numeric_limits<int>::max();
        for (Cell g : validGoals) {
            const int dx = std::abs(c.x - g.x), dy = std::abs(c.y - g.y);
            best = std::min(best, 10 * std::max(dx, dy) + 4 * std::min(dx, dy));
        }
        return best;
    };
    // Among equal-cost routes prefer the straight corridor, keeping parallel lanes parallel.
    const auto deviation = [&](Cell c) {
        if (validGoals.size() != 1) return 0;
        const auto d = validGoals.front() - start, p = c - start;
        return std::abs(d.x * p.y - d.y * p.x);
    };
    struct Entry { int f; int g; int id; int deviation; };
    const auto compare = [](const Entry& a, const Entry& b) {
        if (a.f != b.f) return a.f > b.f;
        if (a.deviation != b.deviation) return a.deviation > b.deviation;
        if (a.g != b.g) return a.g > b.g;
        return a.id > b.id; // Stable tie breaking for repeatable simulation.
    };
    std::priority_queue<Entry, std::vector<Entry>, decltype(compare)> open(compare);
    const auto count = static_cast<size_t>(map.width()) * map.height();
    std::vector<int> cost(count, std::numeric_limits<int>::max()), parent(count, -1);
    cost[index(start)] = 0;
    open.push({heuristic(start), 0, index(start), 0});
    constexpr std::array<Cell, 8> directions{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
    while (!open.empty()) {
        const auto current = open.top();
        open.pop();
        if (current.g != cost[current.id]) continue;
        const Cell from = cell(current.id);
        if (std::find(validGoals.begin(), validGoals.end(), from) != validGoals.end()) {
            Path path{{}, current.g};
            for (int id = current.id; id != -1; id = parent[id]) path.cells.push_back(cell(id));
            std::reverse(path.cells.begin(), path.cells.end());
            return path;
        }
        for (Cell d : directions) {
            const Cell to = from + d;
            if (!map.canStep(from, to, movement)) continue;
            // canStep already checks terrain and both diagonal sides. Only dynamic
            // occupancy remains; do not repeat the terrain queries for every edge.
            if (!unoccupied(to) || ((d.x && d.y) && (!unoccupied({from.x + d.x, from.y}) || !unoccupied({from.x, from.y + d.y})))) continue;
            const int newCost = current.g + ((d.x && d.y) ? 14 : 10);
            const int next = index(to);
            if (newCost >= cost[next]) continue;
            cost[next] = newCost;
            parent[next] = current.id;
            open.push({newCost + heuristic(to), newCost, next, deviation(to)});
        }
    }
    return std::nullopt;
}
std::optional<Path> findPath(const Map& map, Cell start, Cell goal, MovementType movement) {
    return findPath(map, start, std::span<const Cell>(&goal, 1), {}, movement);
}
std::vector<Cell> perimeter(const Map& map, Cell p, int width, int height, MovementType movement) {
    std::vector<Cell> result;
    // No diagonal-only harvesting or depositing through a corner.
    const auto add = [&](Cell c, Cell edge) {
        if (map.walkable(c, movement) && (airborne(movement) || (map.at(c).height == map.at(edge).height && map.at(c).ramp == Cell{})))
            result.push_back(c);
    };
    for (int x = 0; x < width; ++x) {
        add(p + Cell{x, -1}, p + Cell{x, 0});
        add(p + Cell{x, height}, p + Cell{x, height - 1});
    }
    for (int y = 0; y < height; ++y) {
        add(p + Cell{-1, y}, p + Cell{0, y});
        add(p + Cell{width, y}, p + Cell{width - 1, y});
    }
    return result;
}
}
