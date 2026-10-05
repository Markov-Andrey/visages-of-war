#include "rts/Simulation.hpp"
#include <algorithm>
#include <array>

namespace rts {
namespace {
float formationDepth(const Unit& u, Vec2 p) { return p.x * u.formationForward.x + p.y * u.formationForward.y; }
std::pair<float, float> roleBand(const Unit& u, std::span<const Unit> units) {
    const float own = formationDepth(u, u.currentOrder.position.value_or(u.routeDestination));
    float ownMin = own, ownMax = own, front = 1e9f, back = -1e9f;
    for (const auto& other : units) if (other.moveGroup == u.moveGroup) {
        const float d = formationDepth(u, other.currentOrder.position.value_or(other.routeDestination));
        if (other.definition.formationPriority < u.definition.formationPriority) front = std::min(front, d);
        else if (other.definition.formationPriority > u.definition.formationPriority) back = std::max(back, d);
        else { ownMin = std::min(ownMin, d); ownMax = std::max(ownMax, d); }
    }
    return {(ownMin + back) * .5f, (ownMax + front) * .5f};
}
// A cell-based detour may not connect to an interior fractional slot after the
// crowd settles. Find another continuous place in the same role's depth band.
std::optional<Path> relaxedFormationPath(const Simulation& game, const Unit& u, std::span<const Circle> bodies) {
    const auto [back, front] = roleBand(u, game.units());
    std::vector<Vec2> goals;
    const auto clear = [&](Vec2 from, Vec2 to) {
        return std::none_of(bodies.begin(), bodies.end(), [&](Circle body) {
            return sweptCircleIntersects(from, to, u.definition.collisionRadius, body);
        });
    };
    for (const auto& other : game.units()) if (other.moveGroup == u.moveGroup && other.state == UnitState::Idle) {
        const float spacing = u.definition.collisionRadius + other.definition.collisionRadius + .08f;
        for (int i = 0; i < 24; ++i) {
            const float angle = i * .261799388f;
            const Vec2 p = other.position + Vec2{std::cos(angle), std::sin(angle)} * spacing;
            const float d = formationDepth(u, p);
            if (d < back || d > front || lengthSquared(p - center(u.groupTarget)) > (u.groupRadius + .7f) * (u.groupRadius + .7f)) continue;
            if (clear(p, p) && game.map().canTraverse(p, p, u.definition.collisionRadius, u.definition.movement)) goals.push_back(p);
        }
    }
    const Vec2 original = u.currentOrder.position.value_or(u.routeDestination);
    const auto score = [&](Vec2 p) { return lengthSquared(p - u.position) + .5f * lengthSquared(p - original); };
    std::stable_sort(goals.begin(), goals.end(), [&](Vec2 a, Vec2 b) { return score(a) < score(b); });
    for (Vec2 p : goals) if (clear(u.position, p) && game.map().canTraverse(u.position, p, u.definition.collisionRadius, u.definition.movement))
        return Path{{u.cell, cellAt(p)}, 0, p};
    for (size_t i = 0; i < goals.size(); ++i)
        if (auto path = findUnitPathTo(game.map(), u.position, goals[i], bodies, u.definition.collisionRadius, u.definition.movement, 256)) return path;
    return {};
}
}
std::vector<Circle> Simulation::unitObstacles(const Unit& self, bool claimDestinations, bool ignoreGroup) const {
    std::vector<Circle> result;
    for (const auto& other : units_) {
        if (other.id == self.id || airborne(other.definition.movement) != airborne(self.definition.movement)) continue;
        if (ignoreGroup && self.moveGroup && self.moveGroup == other.moveGroup && other.next < other.route.size()) continue;
        result.push_back({other.position, other.definition.collisionRadius});
        if (claimDestinations && !other.route.empty()) result.push_back({other.routeDestination, other.definition.collisionRadius});
    }
    return result;
}
std::vector<Cell> Simulation::occupied(const Unit& self, bool claimDestinations, bool ignoreGroup) const {
    std::vector<Cell> result;
    for (const auto& circle : unitObstacles(self, claimDestinations, ignoreGroup)) {
        const float radius = circle.radius + self.definition.collisionRadius;
        const Cell lo = cellAt(circle.position - Vec2{radius, radius}), hi = cellAt(circle.position + Vec2{radius, radius});
        for (int y = lo.y; y <= hi.y; ++y) for (int x = lo.x; x <= hi.x; ++x) {
            const Cell c{x, y};
            if (map().contains(c) && sweptCircleIntersects(center(c), center(c), self.definition.collisionRadius, circle)) result.push_back(c);
        }
    }
    return result;
}
bool Simulation::unitPositionFree(Vec2 position, const EntityDefinition& type, EntityId ignore) const {
    if (!map().canTraverse(position, position, type.collisionRadius, type.movement)) return false;
    for (const auto& other : units_) if (other.id != ignore && airborne(other.definition.movement) == airborne(type.movement) &&
        sweptCircleIntersects(position, position, type.collisionRadius, {other.position, other.definition.collisionRadius})) return false;
    return true;
}
bool Simulation::dynamicStep(const Unit& u, Vec2 to) const {
    if (!map().canTraverse(u.position, to, u.definition.collisionRadius, u.definition.movement)) return false;
    for (const auto& other : units_) {
        if (other.id == u.id || airborne(other.definition.movement) != airborne(u.definition.movement)) continue;
        // Relative sweep also protects interpolated movement when an earlier unit has moved this tick.
        if (sweptCircleIntersects(u.tickPosition - other.tickPosition, to - other.position,
            u.definition.collisionRadius, {{}, other.definition.collisionRadius})) return false;
    }
    return true;
}
void Simulation::followPath(Unit& u, Path path, UnitState state) {
    if (path.cells.empty()) { arrived(u); return; }
    u.routeDestination = path.destination.value_or(center(path.cells.back()));
    u.route = std::move(path.cells); u.next = 0; u.state = state;
    const auto point = [&](size_t i) { return i + 1 == u.route.size() ? u.routeDestination : center(u.route[i]); };
    // The first cell is only the search anchor. Replanning never snaps the actual position.
    if (u.route.size() > 1 && map().canTraverse(u.position, point(1), u.definition.collisionRadius, u.definition.movement)) u.next = 1;
    else if (!u.route.empty() && lengthSquared(u.position - point(0)) < 1e-10f) u.next = 1;
    if (u.next == u.route.size()) arrived(u);
}
bool Simulation::groupArrived(const Unit& u) const {
    if (u.state != UnitState::Moving || u.groupRadius <= 0 ||
        (u.currentOrder.kind != OrderKind::Move && u.currentOrder.kind != OrderKind::AttackMove)) return false;
    if (lengthSquared(u.position - center(u.groupTarget)) > (u.groupRadius + .7f) * (u.groupRadius + .7f)) return false;
    if (lengthSquared(u.position - u.routeDestination) < .0025f) return true;
    // Arrival may relax within a role's band, but must not leave melee behind
    // archers merely because an archer stopped across its personal destination.
    const auto [back, front] = roleBand(u, units_);
    const float current = formationDepth(u, u.position);
    if (current > front || current < back) return false;
    // A nearby friend that has already arrived is a valid edge of the group.
    // Never finish across a wall, because of an enemy, or inside a choke en route.
    for (const auto& other : units_) {
        if (other.id == u.id || other.moveGroup != u.moveGroup || other.state != UnitState::Idle ||
            airborne(other.definition.movement) != airborne(u.definition.movement)) continue;
        const float near = u.definition.collisionRadius + other.definition.collisionRadius + .18f;
        if (lengthSquared(u.position - other.position) <= near * near &&
            sweptCircleIntersects(u.position, u.routeDestination, u.definition.collisionRadius, {other.position, other.definition.collisionRadius}) &&
            map().canTraverse(u.position, other.position, u.definition.collisionRadius, u.definition.movement)) return true;
    }
    return false;
}
void Simulation::yieldFormation(Unit& u) {
    if (u.groupRadius <= 0 || (clock_.elapsedTicks() + u.id) % 12 != 0) return;
    if (std::none_of(units_.begin(), units_.end(), [&](const Unit& other) {
        return other.moveGroup == u.moveGroup && other.definition.formationPriority != u.definition.formationPriority;
    })) return;
    for (const auto& waiting : units_) {
        if (waiting.id == u.id || waiting.moveGroup != u.moveGroup || waiting.state != UnitState::Moving || waiting.blockedTicks < 30 ||
            waiting.definition.formationPriority > u.definition.formationPriority ||
            airborne(waiting.definition.movement) != airborne(u.definition.movement)) continue;
        const Vec2 away = u.position - waiting.position;
        const float separation = std::sqrt(lengthSquared(away));
        const float radius = u.definition.collisionRadius, clearance = radius + waiting.definition.collisionRadius;
        if (separation > clearance + .35f || separation < .001f) continue;
        const auto [back, front] = roleBand(u, units_);
        const float heading = std::atan2(away.y, away.x), distance = clearance + .2f;
        // Only members of this movement order volunteer to open a lane. Stop,
        // Hold, enemies and unrelated friends never get displaced by the crowd.
        for (int step = 0; step <= 6; ++step) for (float side : {1.0f, -1.0f}) {
            const float angle = heading + side * step * .261799388f;
            const Vec2 p = u.position + Vec2{std::cos(angle), std::sin(angle)} * distance;
            const float d = formationDepth(u, p);
            if (d < back || d > front || lengthSquared(p - center(u.groupTarget)) > (u.groupRadius + .7f) * (u.groupRadius + .7f)) continue;
            if (!map().canTraverse(u.position, p, radius, u.definition.movement)) continue;
            bool clear = true;
            for (const auto& other : units_) if (other.id != u.id && airborne(other.definition.movement) == airborne(u.definition.movement) &&
                sweptCircleIntersects(u.position, p, radius, {other.position, other.definition.collisionRadius})) { clear = false; break; }
            if (!clear) continue;
            u.blockedTicks = 0;
            followPath(u, Path{{u.cell, cellAt(p)}, 0, p}, UnitState::Moving);
            return;
        }
    }
}
void Simulation::moveUnit(Unit& u) {
    const float speed = u.moveGroup ? std::min(u.definition.movementPerSecond, u.groupSpeed) : u.definition.movementPerSecond;
    const float travel = speed / ticksPerSecond;
    if (groupArrived(u)) { arrived(u); return; }
    const bool mixedFormation = u.moveGroup && u.state == UnitState::Moving &&
        std::any_of(units_.begin(), units_.end(), [&](const Unit& other) {
            return other.moveGroup == u.moveGroup && other.definition.formationPriority != u.definition.formationPriority;
        });
    // Replan only after sustained lack of progress. Friends still travelling with
    // this group are not a new maze of walls on each frame.
    if (u.state != UnitState::ToAttack && u.blockedTicks >= 30 && u.blockedTicks % 30 == 0) {
        if (mixedFormation && lengthSquared(u.position - center(u.groupTarget)) < (u.groupRadius + 1) * (u.groupRadius + 1)) {
            if (auto path = relaxedFormationPath(*this, u, unitObstacles(u))) {
                followPath(u, std::move(*path), u.state);
                if (u.next == u.route.size()) return;
            }
        }
        const auto goal = u.routeDestination;
        if (auto path = findUnitPathTo(map(), u.position, goal, unitObstacles(u, false, true),
            u.definition.collisionRadius, u.definition.movement)) {
            followPath(u, std::move(*path), u.state);
            if (u.next == u.route.size()) return;
        } else if (u.blockedTicks >= 90 && (u.state == UnitState::ToHall || u.state == UnitState::ToCrystal)) {
            if (u.state == UnitState::ToHall) returnCargo(u); else seekCrystal(u);
            return;
        }
    }
    // Any-angle steering uses terrain visibility; circular neighbours are handled
    // by the velocity solver, so a moving friend cannot disable path smoothing.
    const auto waypoint = [&](size_t i) { return i + 1 == u.route.size() ? u.routeDestination : center(u.route[i]); };
    const auto clearCombatShortcut = [&](Vec2 point) {
        if (u.state != UnitState::ToAttack && !mixedFormation) return true;
        for (const auto& other : units_) if (other.id != u.id &&
            airborne(other.definition.movement) == airborne(u.definition.movement) && other.next >= other.route.size() &&
            sweptCircleIntersects(u.position, point, u.definition.collisionRadius, {other.position, other.definition.collisionRadius})) return false;
        return true;
    };
    for (size_t candidate = std::min(u.route.size() - 1, u.next + 8); candidate > u.next; --candidate) {
        if (clearCombatShortcut(waypoint(candidate)) && map().canTraverse(u.position, waypoint(candidate), u.definition.collisionRadius, u.definition.movement)) {
            u.next = candidate; break;
        }
    }
    const Vec2 target = waypoint(u.next), delta = target - u.position;
    const float distance = std::sqrt(lengthSquared(delta));
    if (distance < .00001f) { ++u.next; if (u.next == u.route.size()) arrived(u); return; }
    Vec2 preferred = delta * (std::min(speed, distance * ticksPerSecond) / distance);
    // Filter once before testing velocities. Army supply caps crowds at 100;
    // only neighbours that can meet within the prediction horizon participate.
    constexpr float horizon = .7f;
    std::vector<const Unit*> neighbours;
    for (const auto& other : units_) {
        if (other.id == u.id || airborne(other.definition.movement) != airborne(u.definition.movement)) continue;
        const float range = (speed + other.definition.movementPerSecond) * horizon +
            u.definition.collisionRadius + other.definition.collisionRadius + .5f;
        if (lengthSquared(other.tickPosition - u.tickPosition) < range * range) neighbours.push_back(&other);
    }
    const auto dot = [](Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; };
    if (u.moveGroup && u.blockedTicks >= 15) {
        // In a choke, two safe velocities can both be zero. The trailing member
        // yields locally to the front one; a total order prevents reciprocal yields.
        const float progress = dot(u.tickPosition, u.formationForward);
        for (const auto* other : neighbours) {
            if (other->moveGroup != u.moveGroup || other->next >= other->route.size()) continue;
            const float ahead = dot(other->tickPosition, u.formationForward);
            if (ahead < progress || (ahead == progress && other->id > u.id)) continue;
            const Vec2 away = u.tickPosition - other->tickPosition;
            const float separation = std::sqrt(lengthSquared(away));
            const float clearance = u.definition.collisionRadius + other->definition.collisionRadius + .35f;
            if (separation >= clearance || separation < .00001f) continue;
            preferred = away * (speed * .5f / separation);
            break;
        }
    }
    const auto evaluate = [&](Vec2 velocity) {
        const Vec2 next = u.position + velocity * (1.0f / ticksPerSecond);
        if (!map().canTraverse(u.position, next, u.definition.collisionRadius, u.definition.movement)) return 1e9f;
        float penalty = 0;
        // Arrival velocity ends at the destination: extrapolating beyond it
        // would make adjacent valid places repel each other before arrival.
        const float prediction = u.next + 1 == u.route.size() ?
            std::min(horizon, distance / std::max(.01f, std::sqrt(lengthSquared(velocity)))) : horizon;
        for (const auto* other : neighbours) {
            if (sweptCircleIntersects(u.tickPosition - other->tickPosition, next - other->position,
                u.definition.collisionRadius, {{}, other->definition.collisionRadius})) return 1e9f;
            const Vec2 relative = other->tickPosition - u.tickPosition;
            const Vec2 otherVelocity = other->next < other->route.size() ? other->tickVelocity : Vec2{};
            const Vec2 approach = velocity - otherVelocity;
            const float contact = u.definition.collisionRadius + other->definition.collisionRadius;
            // Prediction padding must not prevent a short-range weapon from
            // reaching contact. The actual swept bodies remain unchanged.
            const float padding = other->id == u.targetUnit ? std::clamp(u.definition.attackRange - contact - .015f, 0.0f, .035f) : .035f;
            const float radius = contact + padding;
            const float a = lengthSquared(approach), b = dot(relative, approach), c = lengthSquared(relative) - radius * radius;
            if (b <= 0 || a < .00001f) continue;
            const float discriminant = b * b - a * c;
            if (discriminant <= 0) continue;
            const float time = std::max(0.0f, (b - std::sqrt(discriminant)) / a);
            const float lookahead = u.groupRadius > 0 && other->moveGroup == u.moveGroup && other->state == UnitState::Idle &&
                lengthSquared(u.position - center(u.groupTarget)) < (u.groupRadius + .7f) * (u.groupRadius + .7f) ?
                std::min(prediction, .2f) : prediction;
            if (time < lookahead) penalty = std::max(penalty, 4.0f * (lookahead - time) / lookahead);
        }
        // Prefer progress, then continuity. A slight shared right-hand preference
        // breaks head-on symmetry; hard sweep validation remains authoritative.
        return lengthSquared(velocity - preferred) / (speed * speed) +
            .12f * lengthSquared(velocity - u.tickVelocity) / (speed * speed) + penalty;
    };
    Vec2 velocity = preferred;
    float best = evaluate(velocity);
    if (best > .15f) {
        const auto consider = [&](Vec2 candidate, float bias) {
            const float score = evaluate(candidate) + bias;
            if (score < best) { best = score; velocity = candidate; }
        };
        consider({}, 0);
        const float previousSpeed = std::sqrt(lengthSquared(u.tickVelocity));
        consider(u.tickVelocity * (previousSpeed > std::min(speed, distance * ticksPerSecond) ? std::min(speed, distance * ticksPerSecond) / previousSpeed : 1.0f), .01f);
        const auto direction = delta * (1.0f / distance);
        for (float factor : {1.0f, .5f, .2f, .05f}) for (int step = 0; step <= 12; ++step) {
            const float angle = step * .261799388f;
            for (float side : {1.0f, -1.0f}) {
                const float cosine = std::cos(angle), sine = std::sin(angle) * side;
                consider(Vec2{direction.x * cosine - direction.y * sine, direction.x * sine + direction.y * cosine} *
                    (std::min(speed, distance * ticksPerSecond) * factor), side < 0 ? .025f : 0);
                if (!step) break;
            }
        }
    }
    if (best >= 1e9f) { ++u.blockedTicks; return; }
    const auto displacement = velocity * (1.0f / ticksPerSecond);
    const float travelled = std::sqrt(lengthSquared(displacement));
    const bool tactical = mixedFormation || u.state == UnitState::ToAttack;
    if (travelled < travel * (tactical ? .25f : .1f) || dot(displacement, delta) <= travel * distance * (tactical ? .25f : 0.0f)) ++u.blockedTicks;
    else u.blockedTicks = 0;
    if (travelled < .00001f) return;
    // This final check protects the movement contract even if the scoring or
    // broad phase changes later. No depenetration, teleporting or ally pushing.
    const Vec2 next = u.position + displacement;
    if (!dynamicStep(u, next)) { ++u.blockedTicks; return; }
    u.position = next; u.cell = cellAt(next); u.velocity = velocity;
    u.walkCycle = std::fmod(u.walkCycle + travelled / u.definition.sprite.walkCycleDistance, 1.0f);
    if (lengthSquared(next - target) < 1e-8f) {
        ++u.next;
        if (u.next == u.route.size()) arrived(u);
    }
}
}
