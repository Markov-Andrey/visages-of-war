#include "rts/Formation.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <queue>

namespace rts {
namespace {
constexpr int unreachable = 1000000;
std::vector<int> distances(const Map& map, Cell start, MovementType movement) {
    std::vector<int> costs(static_cast<size_t>(map.width()) * map.height(), unreachable);
    const auto index = [&](Cell c) { return c.y * map.width() + c.x; };
    using Entry = std::pair<int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    costs[index(start)] = 0; queue.push({0, index(start)});
    constexpr std::array<Cell, 8> steps{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
    while (!queue.empty()) {
        const auto [cost, id] = queue.top(); queue.pop();
        if (cost != costs[id]) continue;
        const Cell from{id % map.width(), id / map.width()};
        for (Cell step : steps) {
            const Cell to = from + step;
            if (!map.canStep(from, to, movement)) continue;
            const int nextCost = cost + (step.x && step.y ? 14 : 10);
            if (nextCost >= costs[index(to)]) continue;
            costs[index(to)] = nextCost; queue.push({nextCost, index(to)});
        }
    }
    return costs;
}
// Rectangular minimum-cost assignment. Extra dummy columns allow unreachable members to stay put.
std::vector<size_t> assign(const std::vector<std::vector<int64_t>>& costs) {
    const size_t n = costs.size(), m = costs.front().size();
    const int64_t infinity = std::numeric_limits<int64_t>::max() / 4;
    std::vector<int64_t> u(n + 1), v(m + 1);
    std::vector<size_t> row(m + 1), previous(m + 1);
    for (size_t i = 1; i <= n; ++i) {
        row[0] = i;
        size_t column = 0;
        std::vector<int64_t> minimum(m + 1, infinity);
        std::vector<bool> used(m + 1);
        do {
            used[column] = true;
            const size_t active = row[column];
            int64_t delta = infinity;
            size_t next = 0;
            for (size_t j = 1; j <= m; ++j) if (!used[j]) {
                const int64_t cost = costs[active - 1][j - 1] - u[active] - v[j];
                if (cost < minimum[j]) { minimum[j] = cost; previous[j] = column; }
                if (minimum[j] < delta) { delta = minimum[j]; next = j; }
            }
            for (size_t j = 0; j <= m; ++j) {
                if (used[j]) { u[row[j]] += delta; v[j] -= delta; }
                else minimum[j] -= delta;
            }
            column = next;
        } while (row[column] != 0);
        do {
            const size_t next = previous[column];
            row[column] = row[next]; column = next;
        } while (column != 0);
    }
    std::vector<size_t> result(n);
    for (size_t j = 1; j <= m; ++j) if (row[j]) result[row[j] - 1] = j - 1;
    return result;
}
}
std::vector<FormationDestination> planFormation(const Map& map, std::span<const FormationMember> input,
                                               Cell target, std::span<const FormationObstacle> held) {
    if (input.empty() || !map.contains(target)) return {};
    std::vector<FormationMember> members(input.begin(), input.end());
    std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::erase_if(members, [&](const auto& m) { return !map.walkable(m.start, m.movement); });
    members.erase(std::unique(members.begin(), members.end(), [](const auto& a, const auto& b) { return a.id == b.id; }), members.end());
    if (members.empty()) return {};
    const auto free = [&](Cell c, MovementType movement) {
        return map.walkable(c, movement) && std::none_of(held.begin(), held.end(), [&](const auto& obstacle) {
            return obstacle.cell == c && obstacle.air == airborne(movement);
        });
    };
    Vec2 mean{}, forward{};
    for (const auto& m : members) { mean = mean + Vec2{float(m.start.x), float(m.start.y)}; forward = forward + m.forward; }
    mean = mean * (1.0f / members.size());
    const Vec2 delta{target.x - mean.x, target.y - mean.y};
    if (std::hypot(delta.x, delta.y) > .75f) forward = delta;
    if (std::hypot(forward.x, forward.y) < .001f) forward = {0, 1};
    forward = forward * (1.0f / std::hypot(forward.x, forward.y));
    // Rasterize ranks along the dominant grid axis. Adjacent ranks advance one
    // cell, while the minor axis makes a staircase for diagonal headings.
    const float major = std::max(std::abs(forward.x), std::abs(forward.y));
    const Vec2 along{forward.x / major, forward.y / major};
    const Vec2 side = std::abs(forward.x) >= std::abs(forward.y) ?
        Vec2{0, forward.x > 0 ? 1.0f : -1.0f} : Vec2{forward.y > 0 ? -1.0f : 1.0f, 0};
    const auto index = [&](Cell c) { return c.y * map.width() + c.x; };
    const auto rounded = [](Vec2 p) { return Cell{int(std::floor(p.x + .5f)), int(std::floor(p.y + .5f))}; };
    constexpr int64_t stayCost = 1000000000000LL, impossible = stayCost * 100;
    std::vector<std::vector<int>> fields;
    for (const auto& m : members) fields.push_back(distances(map, m.start, m.movement));

    // Each priority begins a new rank. An odd last rank has one centred member.
    std::vector<int> priorities;
    for (const auto& m : members) priorities.push_back(m.priority);
    std::sort(priorities.begin(), priorities.end());
    struct Slot { Vec2 offset; int priority; };
    std::vector<Slot> slots;
    int rank = 0;
    for (size_t begin = 0; begin < priorities.size();) {
        size_t end = begin;
        while (end < priorities.size() && priorities[end] == priorities[begin]) ++end;
        for (size_t i = begin; i < end; i += 2, ++rank) {
            const int count = i + 1 < end ? 2 : 1;
            const Cell base = rounded(along * float(-rank));
            for (int column = 0; column < count; ++column)
                slots.push_back({Vec2{float(base.x), float(base.y)} + side * float(column), priorities[begin]});
        }
        begin = end;
    }
    // Snap the shape first, then translate it as a whole. Independent rounding
    // around the target could merge dense diagonal slots or split a straight rank.
    std::vector<Cell> ideal;
    Vec2 offsetMean{};
    for (const auto& slot : slots) {
        const Cell c = rounded(slot.offset);
        ideal.push_back(c); offsetMean = offsetMean + Vec2{float(c.x), float(c.y)};
    }
    offsetMean = offsetMean * (1.0f / slots.size());
    const Cell translation = rounded(Vec2{float(target.x), float(target.y)} - offsetMean);
    for (auto& c : ideal) c = c + translation;

    // Assign ranks by role, then minimize travel inside that role. IDs are only tie breakers.
    std::vector<std::vector<int64_t>> costs(members.size(), std::vector<int64_t>(slots.size(), impossible));
    for (size_t i = 0; i < members.size(); ++i) for (size_t j = 0; j < slots.size(); ++j) {
        if (members[i].priority != slots[j].priority) continue;
        const Cell d = ideal[j] - members[i].start;
        // Rank assignment is geometric even if an ideal slot hits an obstacle.
        // Giving unreachable slots a flat cost would send an arbitrary rear unit to the front.
        costs[i][j] = 10LL * (d.x * d.x + d.y * d.y);
    }
    const auto rankAssignment = assign(costs);
    bool fits = true;
    for (size_t i = 0; i < members.size(); ++i) {
        const Cell c = ideal[rankAssignment[i]];
        if (!free(c, members[i].movement) || fields[i][index(c)] == unreachable) fits = false;
    }
    std::vector<FormationDestination> result;
    if (fits) {
        for (size_t i = 0; i < members.size(); ++i) result.push_back({members[i].id, ideal[rankAssignment[i]], forward});
        return result;
    }

    // Obstacles may deform the ranks. Prefer reachable cells near each member's own slot.
    // Unique real columns reserve destinations; dummy columns leave unreachable members alone.
    std::vector<Cell> candidates;
    const int radius = std::max(8, rank + 3);
    for (int y = std::max(0, target.y - radius); y <= std::min(map.height() - 1, target.y + radius); ++y)
        for (int x = std::max(0, target.x - radius); x <= std::min(map.width() - 1, target.x + radius); ++x) {
            const Cell c{x, y};
            if (std::any_of(members.begin(), members.end(), [&](const auto& m) { return free(c, m.movement); })) candidates.push_back(c);
        }
    costs.assign(members.size(), std::vector<int64_t>(candidates.size() + members.size(), stayCost));
    for (size_t i = 0; i < members.size(); ++i) for (size_t j = 0; j < candidates.size(); ++j) {
        const Cell c = candidates[j], d = c - ideal[rankAssignment[i]];
        const int64_t length = fields[i][index(c)];
        costs[i][j] = !free(c, members[i].movement) || length == unreachable ? impossible :
            10000LL * (d.x * d.x + d.y * d.y) + length * length;
    }
    const auto assignments = assign(costs);
    for (size_t i = 0; i < members.size(); ++i) if (assignments[i] < candidates.size() && costs[i][assignments[i]] < stayCost)
        result.push_back({members[i].id, candidates[assignments[i]], forward});
    return result;
}
}
