#include "rts/Simulation.hpp"
#include <algorithm>

namespace rts {
bool Simulation::returnCargo(Unit& u) {
    std::optional<Path> best;
    EntityId depot{};
    for (const auto& b : buildings_) {
        if (!b.complete() || !b.definition.acceptsCargo || b.owner != u.owner) continue;
        auto path = findPath(map(), u.cell, perimeter(map(), b.origin, b.definition.width, b.definition.height, u.definition.movement), occupied(u, true), u.definition.movement);
        if (!path) path = findPath(map(), u.cell, perimeter(map(), b.origin, b.definition.width, b.definition.height, u.definition.movement), {}, u.definition.movement);
        if (path && (!best || path->cost < best->cost)) { best = std::move(path); depot = b.id; }
    }
    if (!best) { u.state = UnitState::Idle; u.route.clear(); message_ = L"Нет пути к ратуше. Груз сохранён."; return false; }
    u.targetBuilding = depot;
    const Cell goal = best->cells.back();
    return setRoute(u, std::span<const Cell>(&goal, 1), UnitState::ToHall);
}
bool Simulation::seekCrystal(Unit& u) {
    u.route.clear(); u.next = 0; u.progress = 0; u.blockedTicks = 0;
    if (!u.repeatGather || u.gatherOriginCrystal < 0) { u.state = UnitState::Idle; return false; }
    const Cell origin = crystals()[u.gatherOriginCrystal].cell;
    // Never move this search origin to a fallback deposit: repeated exhaustion must
    // not walk the worker from one resource field into a completely different one.
    constexpr int neighborRadius = 3;
    const auto distance = [&](size_t i) {
        const Cell d = crystals()[i].cell - origin;
        return d.x * d.x + d.y * d.y;
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
        u.route = std::move(path.cells); u.next = 1; u.state = UnitState::ToCrystal;
        if (u.next == u.route.size()) arrived(u);
        return true;
    };
    for (size_t target : candidates) {
        auto slots = perimeter(map(), crystals()[target].cell, 1, 1, u.definition.movement);
        if (!waitingPath) {
            waitingPath = findPath(map(), u.cell, slots, {}, u.definition.movement);
            if (waitingPath) waitingTarget = static_cast<int>(target);
        }
        // A busy approach is different from a busy harvesting slot. Reserve a free
        // destination even when temporary traffic prevents reaching it right now.
        std::erase_if(slots, [&](Cell c) { return std::find(held.begin(), held.end(), c) != held.end(); });
        auto path = findPath(map(), u.cell, slots, held, u.definition.movement);
        if (!path) path = findPath(map(), u.cell, slots, {}, u.definition.movement);
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
