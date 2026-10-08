#include "rts/Simulation.hpp"
#include <algorithm>

namespace rts {
bool Simulation::returnCargo(Unit& u) {
    std::optional<Path> best;
    EntityId depot{};
    for (const auto& b : buildings_) {
        if (!b.complete() || !b.definition.acceptsCargo || b.owner != u.owner) continue;
        auto path = findUnitPath(map(), u.position, perimeter(map(), b.origin, b.definition.width, b.definition.height, u.definition.movement), unitObstacles(u, true), u.definition.collisionRadius, u.definition.movement);
        if (!path) path = findUnitPath(map(), u.position, perimeter(map(), b.origin, b.definition.width, b.definition.height, u.definition.movement), {}, u.definition.collisionRadius, u.definition.movement);
        if (path && (!best || path->cost < best->cost)) { best = std::move(path); depot = b.id; }
    }
    if (!best) { u.state = UnitState::Idle; u.route.clear(); setMessage(L"Нет пути к ратуше. Груз сохранён."); return false; }
    u.targetBuilding = depot;
    const Cell goal = best->cells.back();
    return setRoute(u, std::span<const Cell>(&goal, 1), UnitState::ToHall);
}
bool Simulation::seekCrystal(Unit& u) {
    u.route.clear(); u.next = 0; u.blockedTicks = 0;
    if (!u.repeatGather || u.gatherOriginCrystal < 0) { u.state = UnitState::Idle; return false; }
    const auto& origin = crystals()[u.gatherOriginCrystal];
    // Never move this search origin to a fallback deposit: repeated exhaustion must
    // not walk the worker from one resource field into a completely different one.
    constexpr int neighborRadius = 3;
    const auto distance = [&](size_t i) {
        const auto& node = crystals()[i];
        const float left = float(node.cell.x - (origin.cell.x + origin.width - 1));
        const float right = float(node.cell.x + node.width - 1 - origin.cell.x);
        const float top = float(node.cell.y - (origin.cell.y + origin.height - 1));
        const float bottom = float(node.cell.y + node.height - 1 - origin.cell.y);
        if (left <= 0 && right >= 0 && top <= 0 && bottom >= 0) return 0.0f;
        // Closest points of the two grid footprints in the ground-plane metric.
        return std::min({groundLengthSquared({left, std::clamp(.6f * left, top, bottom)}),
            groundLengthSquared({right, std::clamp(.6f * right, top, bottom)}),
            groundLengthSquared({std::clamp(.6f * top, left, right), top}),
            groundLengthSquared({std::clamp(.6f * bottom, left, right), bottom})});
    };
    std::vector<size_t> candidates;
    for (size_t i = 0; i < crystals().size(); ++i)
        if (crystals()[i].remaining > 0 && (int(i) == u.gatherOriginCrystal || knownCrystals_[i] > 0) &&
            distance(i) <= neighborRadius * neighborRadius) candidates.push_back(i);
    std::sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) {
        return distance(a) != distance(b) ? distance(a) < distance(b) : a < b;
    });
    if (candidates.empty()) {
        u.targetCrystal = -1; u.repeatGather = false; u.state = UnitState::Idle;
        return false;
    }
    const auto held = occupied(u, true);
    std::optional<Path> waitingPath;
    int waitingTarget = -1;
    const auto follow = [&](size_t target, Path path) {
        u.targetCrystal = static_cast<int>(target);
        followPath(u, std::move(path), UnitState::ToCrystal);
        return true;
    };
    for (size_t target : candidates) {
        const auto& node = crystals()[target];
        auto slots = perimeter(map(), node.cell, node.width, node.height, u.definition.movement);
        if (!waitingPath) {
            waitingPath = findUnitPath(map(), u.position, slots, {}, u.definition.collisionRadius, u.definition.movement);
            if (waitingPath) waitingTarget = static_cast<int>(target);
        }
        // A busy approach is different from a busy harvesting slot. Reserve a free
        // destination even when temporary traffic prevents reaching it right now.
        std::erase_if(slots, [&](Cell c) { return std::find(held.begin(), held.end(), c) != held.end(); });
        auto path = findUnitPath(map(), u.position, slots, unitObstacles(u, true), u.definition.collisionRadius, u.definition.movement);
        if (!path) path = findUnitPath(map(), u.position, slots, {}, u.definition.collisionRadius, u.definition.movement);
        if (path) return follow(target, std::move(*path));
    }
    // All local slots are occupied: approach this field and wait, preserving the
    // command. If terrain prevents any approach, retry in place until it opens.
    if (waitingPath) return follow(static_cast<size_t>(waitingTarget), std::move(*waitingPath));
    u.targetCrystal = static_cast<int>(candidates.front());
    u.state = UnitState::WaitingForCrystal;
    return true;
}
}
