#include "rts/Navigation.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <queue>

namespace rts {
namespace {
std::optional<Path> searchPath(const Map& map, Cell start, std::span<const Cell> goals, std::span<const Cell> occupied,
    MovementType movement, Vec2 position, float radius, std::span<const Circle> obstacles, std::optional<Vec2> destination = {}) {
    if (!map.walkable(start, movement) || goals.empty()) return std::nullopt;
    std::vector<bool> blocked(static_cast<size_t>(map.width()) * map.height());
    for (Cell c : occupied) if (map.contains(c) && c != start) blocked[static_cast<size_t>(c.y) * map.width() + c.x] = true;
    const auto clear = [&](Vec2 from, Vec2 to) {
        return std::none_of(obstacles.begin(), obstacles.end(), [&](Circle other) { return sweptCircleIntersects(from, to, radius, other); });
    };
    const auto point = [&](Cell c) { return destination && c == goals.front() ? *destination : center(c); };
    const auto free = [&](Cell c) { return map.walkable(c, movement) && !blocked[static_cast<size_t>(c.y) * map.width() + c.x] &&
        (radius == 0 || clear(point(c), point(c))); };
    std::vector<Cell> validGoals;
    for (Cell goal : goals) if (free(goal) && (radius == 0 || goal != start ||
        (clear(position, point(goal)) && map.canTraverse(position, point(goal), radius, movement)))) validGoals.push_back(goal);
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
            Path path{{}, current.g, destination};
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
            if (radius > 0) {
                const auto origin = from == start ? position : center(from);
                if (!clear(origin, point(to)) || ((from == start || (destination && to == goals.front())) &&
                    !map.canTraverse(origin, point(to), radius, movement))) continue;
            }
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
}
std::optional<Path> findPath(const Map& map, Cell start, std::span<const Cell> goals, std::span<const Cell> occupied, MovementType movement) {
    return searchPath(map, start, goals, occupied, movement, center(start), 0, {});
}
std::optional<Path> findUnitPath(const Map& map, Vec2 start, std::span<const Cell> goals,
    std::span<const Circle> obstacles, float radius, MovementType movement) {
    if (!std::isfinite(radius) || radius < .05f || radius > .5f || !map.canTraverse(start, start, radius, movement)) return std::nullopt;
    return searchPath(map, cellAt(start), goals, {}, movement, start, radius, obstacles);
}
std::optional<Path> findUnitPathTo(const Map& map, Vec2 start, Vec2 goal,
    std::span<const Circle> obstacles, float radius, MovementType movement) {
    if (!std::isfinite(radius) || radius < .05f || radius > .5f ||
        !map.canTraverse(start, start, radius, movement) || !map.canTraverse(goal, goal, radius, movement)) return std::nullopt;
    const Cell cell = cellAt(goal);
    return searchPath(map, cellAt(start), std::span<const Cell>(&cell, 1), {}, movement, start, radius, obstacles, goal);
}
std::optional<Path> findPath(const Map& map, Cell start, Cell goal, MovementType movement) {
    return findPath(map, start, std::span<const Cell>(&goal, 1), {}, movement);
}
RouteField makeRouteField(const Map& map, std::span<const Cell> goals, MovementType movement) {
    RouteField field{map.width(), movement, std::vector<int>(static_cast<size_t>(map.width()) * map.height(), -1),
        std::vector<Cell>(static_cast<size_t>(map.width()) * map.height())};
    const auto index = [&](Cell c) { return c.y * map.width() + c.x; };
    using Entry = std::pair<int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    for (Cell goal : goals) if (map.walkable(goal, movement)) {
        const auto id = index(goal);
        field.costs[id] = 0; field.next[id] = goal; open.push({0, id});
    }
    constexpr std::array<Cell, 8> steps{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
    while (!open.empty()) {
        const auto [cost, id] = open.top(); open.pop();
        if (cost != field.costs[id]) continue;
        const Cell from{id % map.width(), id / map.width()};
        for (Cell d : steps) {
            const Cell to = from + d;
            if (!map.canStep(to, from, movement)) continue;
            const int nextCost = cost + (d.x && d.y ? 14 : 10), nextId = index(to);
            if (field.costs[nextId] >= 0 && field.costs[nextId] <= nextCost) continue;
            field.costs[nextId] = nextCost; field.next[nextId] = from; open.push({nextCost, nextId});
        }
    }
    return field;
}
std::optional<Path> fieldPath(const Map& map, const RouteField& field, Vec2 start, Cell goal, float radius) {
    if (!map.canTraverse(start, start, radius, field.movement)) return std::nullopt;
    Cell c = cellAt(start);
    const auto index = [&](Cell cell) { return cell.y * field.width + cell.x; };
    if (field.width != map.width() || field.costs.size() != static_cast<size_t>(map.width()) * map.height() ||
        field.costs[index(c)] < 0) return std::nullopt;
    Path result{{c}, field.costs[index(c)]};
    while (field.costs[index(c)] > 0) {
        const Cell next = field.next[index(c)];
        if (!map.canStep(c, next, field.movement)) return std::nullopt;
        result.cells.push_back(next); c = next;
    }
    if (c != goal) {
        auto tail = findPath(map, c, goal, field.movement);
        if (!tail) return std::nullopt;
        result.cost += tail->cost;
        result.cells.insert(result.cells.end(), tail->cells.begin() + 1, tail->cells.end());
    }
    return result;
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
