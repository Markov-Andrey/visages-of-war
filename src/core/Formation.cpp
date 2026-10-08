#include "rts/Formation.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <queue>

namespace rts {
namespace {
// Terrain connectivity is shared by members of the same movement type. A group
// command visits each connected region once instead of running N full Dijkstras.
class Regions {
public:
    explicit Regions(const Map& map) : map_(map) {}
    int at(Cell start, MovementType movement) {
        if (!map_.walkable(start, movement)) return -1;
        auto& labels = labels_[static_cast<size_t>(movement)];
        if (labels.empty()) labels.resize(static_cast<size_t>(map_.width()) * map_.height());
        const auto index = [&](Cell c) { return c.y * map_.width() + c.x; };
        if (labels[index(start)]) return labels[index(start)];
        const int label = ++next_;
        std::vector<Cell> queue{start}; labels[index(start)] = label;
        constexpr std::array<Cell, 4> steps{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
        for (size_t i = 0; i < queue.size(); ++i) for (Cell step : steps) {
            const auto to = queue[i] + step;
            if (!map_.canStep(queue[i], to, movement) || labels[index(to)]) continue;
            labels[index(to)] = label; queue.push_back(to);
        }
        return label;
    }
private:
    const Map& map_;
    std::array<std::vector<int>, 4> labels_;
    int next_{};
};
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
    const auto free = [&](Vec2 p, MovementType movement, float radius) {
        return map.canTraverse(p, p, radius, movement) && std::none_of(held.begin(), held.end(), [&](const auto& obstacle) {
            return obstacle.air == airborne(movement) && sweptCircleIntersects(p, p, radius,
                {obstacle.position.value_or(center(obstacle.cell)), obstacle.radius});
        });
    };
    Vec2 mean{}, forward{};
    for (const auto& m : members) { mean = mean + m.position.value_or(center(m.start)) - Vec2{.5f, .5f}; forward = forward + m.forward; }
    mean = mean * (1.0f / members.size());
    const Vec2 delta{target.x - mean.x, target.y - mean.y};
    if (groundLength(delta) > .75f) forward = delta;
    if (groundLength(forward) < .001f) forward = {0, 1};
    forward = forward * (1.0f / groundLength(forward));
    Regions regions(map);
    std::vector<int> components;
    for (const auto& m : members) components.push_back(regions.at(m.start, m.movement));
    const auto reachable = [&](size_t i, Vec2 p) {
        return free(p, members[i].movement, members[i].radius) && regions.at(cellAt(p), members[i].movement) == components[i];
    };
    constexpr int64_t stayCost = 1000000000000LL, impossible = stayCost * 100;
    // Hexagonal circle packing in the orthonormal ground plane, sized from the bodies.
    // Grid cells only identify terrain; several small bodies may share one cell.
    float spacing = .1f;
    for (const auto& member : members) spacing = std::max(spacing, 2 * member.radius + .08f);
    std::vector<Vec2> candidates;
    const int rings = std::max(10, int(std::ceil(std::sqrt(float(members.size())))) + 4);
    const Vec2 origin = center(target);
    for (int y = -rings; y <= rings; ++y) for (int x = -rings; x <= rings; ++x) {
        const Vec2 p = origin + planeToGround({(x + y * .5f) * spacing, y * .866025404f * spacing});
        for (size_t i = 0; i < members.size(); ++i) if (reachable(i, p)) { candidates.push_back(p); break; }
    }
    const auto radial = [&](Vec2 p) { return groundLengthSquared(p - origin); };
    const auto depth = [&](Vec2 p) { return groundDot(p, forward); };
    std::sort(candidates.begin(), candidates.end(), [&](Vec2 a, Vec2 b) {
        const int ra = int(std::lround(radial(a) * 10000)), rb = int(std::lround(radial(b) * 10000));
        if (ra != rb) return ra < rb;
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    std::vector<Vec2> ideal(candidates.begin(), candidates.begin() + std::min(candidates.size(), members.size()));
    std::sort(ideal.begin(), ideal.end(), [&](Vec2 a, Vec2 b) {
        if (depth(a) != depth(b)) return depth(a) > depth(b);
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    std::vector<int> priorities;
    for (const auto& m : members) priorities.push_back(m.priority);
    std::sort(priorities.begin(), priorities.end());
    std::vector<std::vector<int64_t>> costs(members.size(), std::vector<int64_t>(ideal.size() + members.size(), stayCost));
    for (size_t i = 0; i < members.size(); ++i) for (size_t j = 0; j < ideal.size(); ++j) {
        const auto d = ideal[j] - members[i].position.value_or(center(members[i].start));
        costs[i][j] = members[i].priority == priorities[j] && reachable(i, ideal[j]) ?
            int64_t(std::llround(1000 * groundLengthSquared(d))) : impossible;
    }
    auto assignment = assign(costs);
    const bool fits = std::all_of(assignment.begin(), assignment.end(), [&](size_t j) { return j < ideal.size(); });
    if (!fits) {
        // Water, walls and held units can deform the disk; keep all reachable
        // members near the clicked point, even when their terrain differs.
        costs.assign(members.size(), std::vector<int64_t>(candidates.size() + members.size(), stayCost));
        for (size_t i = 0; i < members.size(); ++i) for (size_t j = 0; j < candidates.size(); ++j) {
            const auto d = candidates[j] - members[i].position.value_or(center(members[i].start));
            costs[i][j] = reachable(i, candidates[j]) ? int64_t(std::llround(1000000 * radial(candidates[j]) + 1000 * groundLengthSquared(d))) : impossible;
        }
        assignment = assign(costs);
        ideal = candidates;
    }
    std::vector<FormationDestination> result;
    for (size_t i = 0; i < members.size(); ++i) if (assignment[i] < ideal.size() && costs[i][assignment[i]] < stayCost)
        result.push_back({members[i].id, cellAt(ideal[assignment[i]]), forward, ideal[assignment[i]]});
    return result;
}
}
