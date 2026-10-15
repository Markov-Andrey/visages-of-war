#include "rts/Simulation.hpp"
#include <algorithm>

namespace rts {
void Simulation::harvest(Entity& u, const HarvestDefinition& effect, AbilityEffectState& state) {
    auto& node = scenario_.crystals[static_cast<size_t>(u.gathering().targetCrystal)];
    if (++state.ticks < effect.intervalTicks) return;
    state.ticks = 0;
    const bool direct = effect.credit == HarvestCredit::Direct;
    const int amount = std::min({effect.amount, node.remaining,
        direct ? node.remaining : std::max(0, u.definition.carryCapacity - u.gathering().cargo)});
    node.remaining -= amount;
    if (direct) {
        mutableEconomy(u.owner).crystals += amount;
        if (amount > 0) events_.emplace_back(CrystalsHarvested{u.id, u.owner, u.position, unitHeight(u), amount, clock_.elapsedTicks()});
    } else u.gathering().cargo += amount;
    if (node.remaining == 0)
        for (int y = 0; y < node.height; ++y) for (int x = 0; x < node.width; ++x)
            scenario_.map.release(node.cell + Cell{x, y});
}
bool Simulation::returnCargo(Unit& u) {
    AbilityDefinition delivery; delivery.targeting = AbilityTargeting::Entity;
    std::optional<Path> best;
    EntityId depot{};
    for (const auto* candidate : entities()) {
        const auto& b = *candidate;
        if (b.id == u.id || b.vitals().health <= 0 || !b.complete() || !b.definition.acceptsCargo || b.owner != u.owner) continue;
        if (abilityReach(u, delivery, b.id, u.position)) {
            mutableEconomy(u.owner).crystals += u.gathering().cargo; u.gathering().cargo = 0;
            u.motion().route.clear(); u.motion().next = 0; u.orders().state = UnitState::Idle; u.orders().targetBuilding = 0;
            if (u.gathering().repeatGather) seekCrystal(u);
            return true;
        }
        if (!u.definition.mobile) continue;
        const auto positions = abilityPositions(u, delivery, b.id);
        auto path = findUnitPath(map(), u.position, positions, unitObstacles(u, true), u.definition.collisionRadius, u.definition.movement);
        if (!path) path = findUnitPath(map(), u.position, positions, {}, u.definition.collisionRadius, u.definition.movement);
        if (path && (!best || path->cost < best->cost)) { best = std::move(path); depot = b.id; }
    }
    u.motion().route.clear(); u.motion().next = 0;
    if (!best) { u.orders().state = UnitState::Idle; setMessage(L"Нет пути к складу. Груз сохранён."); return false; }
    u.orders().targetBuilding = depot; u.combat().chaseTicks = 15;
    followPath(u, std::move(*best), UnitState::ToHall);
    return true;
}
bool Simulation::seekCrystal(Unit& u) {
    u.motion().route.clear(); u.motion().next = 0; u.motion().blockedTicks = 0;
    if (!u.gathering().repeatGather || u.gathering().gatherOriginCrystal < 0) { u.orders().state = UnitState::Idle; return false; }
    const auto& origin = crystals()[u.gathering().gatherOriginCrystal];
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
        if (crystals()[i].remaining > 0 && (int(i) == u.gathering().gatherOriginCrystal ||
            (u.owner == player_.id ? knownCrystals_[i] > 0 : crystalVisibleTo(u.owner, crystals()[i]))) &&
            distance(i) <= neighborRadius * neighborRadius) candidates.push_back(i);
    std::sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) {
        return distance(a) != distance(b) ? distance(a) < distance(b) : a < b;
    });
    if (candidates.empty()) {
        u.gathering().targetCrystal = -1; u.gathering().repeatGather = false; u.orders().state = UnitState::Idle;
        return false;
    }
    const auto held = occupied(u, true);
    std::optional<Path> waitingPath;
    int waitingTarget = -1;
    const auto follow = [&](size_t target, Path path) {
        u.gathering().targetCrystal = static_cast<int>(target);
        followPath(u, std::move(path), UnitState::ToCrystal);
        return true;
    };
    for (size_t target : candidates) {
        const auto& node = crystals()[target];
        const auto* ability = u.harvestAbility();
        if (!ability) return false;
        if (abilityReach(u, *ability, node.id, u.position)) {
            u.gathering().targetCrystal = static_cast<int>(target); u.orders().state = UnitState::Harvesting; u.gathering().harvestTicks = 0;
            return true;
        }
        if (!u.definition.mobile) continue;
        auto slots = abilityPositions(u, *ability, node.id);
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
    u.gathering().targetCrystal = static_cast<int>(candidates.front());
    u.orders().state = UnitState::WaitingForCrystal;
    return true;
}
}
