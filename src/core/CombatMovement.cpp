#include "rts/Simulation.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace rts {
void Simulation::approachCombat(Unit& u, Vec2 aim, const CombatTarget* target) {
    // Reconsider at staggered intervals, but retain a useful route and its progress.
    u.chaseTicks = 12 + int(u.id % 7);
    const float radius = u.definition.collisionRadius, range = u.definition.attackRange;
    const auto movement = u.definition.movement;
    const auto bodies = unitObstacles(u);
    std::vector<Circle> claims;
    for (const auto& other : units_) if (other.id != u.id && other.owner == u.owner &&
        airborne(other.definition.movement) == airborne(movement) && other.state == UnitState::ToAttack &&
        other.next < other.route.size()) claims.push_back({other.routeDestination, other.definition.collisionRadius + .025f});
    const auto clear = [&](Vec2 from, Vec2 to) {
        return std::none_of(bodies.begin(), bodies.end(), [&](Circle body) { return sweptCircleIntersects(from, to, radius, body); });
    };
    const auto free = [&](Vec2 p) {
        return clear(p, p) && std::none_of(claims.begin(), claims.end(), [&](Circle claim) {
            return sweptCircleIntersects(p, p, radius, claim);
        }) && map().canTraverse(p, p, radius, movement);
    };
    const auto reaches = [&](Vec2 p) {
        return target ? attackReach(u, *target, p) : groundLengthSquared(p - aim) <= range * range;
    };
    if (u.state == UnitState::ToAttack && u.next < u.route.size() && u.blockedTicks < 12 &&
        reaches(u.routeDestination) && free(u.routeDestination)) return;

    // Aim at continuous positions on the attack disk, not at terrain cell centres.
    // The radial direction gives the shortest approach; additional rings handle
    // cramped terrain and occupied firing positions without changing weapon reach.
    const Vec2 offset = groundToPlane(u.position - aim);
    const float heading = std::atan2(offset.y, offset.x);
    const float contact = target && target->unit && target->air() == airborne(movement) ?
        std::min(range, radius + target->unit->definition.collisionRadius + .045f) :
        target && target->building ? std::min(range, radius + .045f) : std::min(range, .05f);
    const float outer = std::max(contact, range - .06f);
    std::vector<Vec2> goals;
    const auto ring = [&](float distance, bool firing) {
        if (target && target->building) {
            const auto& b = *target->building;
            const float x = float(b.origin.x), y = float(b.origin.y);
            const std::array<Vec2, 4> corners{{{x,y}, {x+b.definition.width,y},
                {x+b.definition.width,y+b.definition.height}, {x,y+b.definition.height}}};
            const auto add = [&](Vec2 p) { if ((!firing || reaches(p)) && free(p)) goals.push_back(p); };
            for (size_t edgeIndex = 0; edgeIndex < corners.size(); ++edgeIndex) {
                const Vec2 a = corners[edgeIndex], edge = corners[(edgeIndex + 1) % corners.size()] - a;
                const Vec2 plane = groundToPlane(edge);
                const Vec2 normal = planeToGround({plane.y, -plane.x}) * (distance / groundLength(edge));
                const int samples = std::clamp(int(std::ceil(groundLength(edge) / (2 * radius + .1f))), 2, 64);
                for (int i = 0; i <= samples; ++i) add(a + edge * (float(i) / samples) + normal);
                add(a + edge * std::clamp(groundDot(u.position - a, edge) / groundLengthSquared(edge), 0.f, 1.f) + normal);
                for (int i = 0; i < 16; ++i) {
                    const float angle = i * (6.283185307f / 16);
                    add(a + planeToGround({std::cos(angle), std::sin(angle)}) * distance);
                }
            }
            return;
        }
        const int count = std::clamp(int(std::ceil(distance * 12)), 24, 64);
        for (int i = 0; i < count; ++i) {
            const float angle = heading + i * (6.283185307f / count);
            const Vec2 p = aim + planeToGround({std::cos(angle), std::sin(angle)}) * distance;
            if ((!firing || reaches(p)) && free(p)) goals.push_back(p);
        }
    };
    ring(outer, true);
    if (outer - contact > .15f) { ring((outer + contact) * .5f, true); ring(contact, true); }
    // A full surround has finite capacity. Extra attackers approach the outside
    // and wait for an opening; their explicit target remains intact.
    const bool staging = goals.empty();
    if (staging) for (int rank = 1; rank <= 3 && goals.empty(); ++rank) ring(outer + rank * (2 * radius + .12f), false);
    if (goals.empty()) {
        u.route.clear(); u.next = 0; u.state = UnitState::ToAttack;
        return;
    }
    const auto score = [&](Vec2 p) {
        const float retry = u.blockedTicks >= 12 && groundLengthSquared(p - u.routeDestination) < .49f ? 4.0f : 0.0f;
        return groundLengthSquared(p - u.position) + retry;
    };
    std::stable_sort(goals.begin(), goals.end(), [&](Vec2 a, Vec2 b) { return score(a) < score(b); });
    if (staging && groundLengthSquared(goals.front() - u.position) < .04f) {
        u.route.clear(); u.next = 0; u.state = UnitState::ToAttack;
        return;
    }
    for (Vec2 goal : goals) if (clear(u.position, goal) && map().canTraverse(u.position, goal, radius, movement)) {
        followPath(u, Path{{u.cell, cellAt(goal)}, 0, goal}, UnitState::ToAttack);
        return;
    }
    // Only obstructed approaches need graph search. Bound dynamic searches and
    // share a deterministic budget across the whole army, including click orders.
    // Deferred requests keep their current route and get another turn next tick.
    if (combatSearchBudget_ <= 0) { u.chaseTicks = 1; u.state = UnitState::ToAttack; return; }
    --combatSearchBudget_;
    const Vec2 goal = goals.front();
    if (auto path = findUnitPathTo(map(), u.position, goal, bodies, radius, movement, 192)) {
        followPath(u, std::move(*path), UnitState::ToAttack);
        return;
    }
    if (map().canTraverse(u.position, goal, radius, movement)) {
        followPath(u, Path{{u.cell, cellAt(goal)}, 0, goal}, UnitState::ToAttack);
        return;
    }
    // Static navigation still follows Map::canStep across ramps, corners and
    // square footprints. Nearby moving bodies are handled by local avoidance.
    if (auto path = findUnitPathTo(map(), u.position, goal, {}, radius, movement, 4096)) {
        followPath(u, std::move(*path), UnitState::ToAttack);
        return;
    }
    u.route.clear(); u.next = 0; u.state = UnitState::ToAttack;
    u.chaseTicks = 30 + int(u.id % 13);
    if (target) { u.unreachableTarget = target->id(); u.targetRetryTicks = 90; }
}
}
