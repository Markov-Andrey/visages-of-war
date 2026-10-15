#include "CombatRules.hpp"
#include <algorithm>

namespace rts {
void Simulation::resolveHits(const std::vector<Hit>& hits) {
    // Resolve this tick's hits together, so equal duels do not favour the lower entity ID.
    std::vector<std::pair<EntityId, PlayerId>> kills;
    for (const auto hit : hits) {
        if (auto* target = mutableEntity(hit.target); target && target->vitals().health > 0) {
            const auto before = target->vitals().health;
            target->vitals().health = std::max(Vital{}, target->vitals().health - armorRules_.damage(hit.damage, target->armor()));
            target->vitals().healthFeedback.record(before, target->vitals().health, clock_.elapsedTicks());
            if (target->vitals().health == 0) kills.emplace_back(target->id, hit.owner);
        }
    }
    // Dead heroes cannot gain levels or heal out of a simultaneous lethal hit.
    for (const auto [victim, killer] : kills) rewardKill(*entity(victim), killer);
    removeDeadEntities();
}
void Simulation::removeDeadEntities() {
    bool deaths = false;
    for (const auto& u : world_.circles()) if (u.vitals().health == 0) {
        if (u.progression().hero && u.owner == player_.id) heroFallen_ = true;
        if (activePlayer(u.owner)) {
            auto& supply = mutableEconomy(u.owner).supply;
            supply.release(u.definition.cost.supply);
            for (const auto& job : u.productionState().production) supply.release(job.reservedSupply);
        }
        leaveRemains(u);
        events_.emplace_back(UnitDied{u.id, u.owner});
        deaths = true;
    }
    std::vector<EntityId> deadUnits;
    for (const auto& u : world_.circles()) if (u.vitals().health == 0) deadUnits.push_back(u.id);
    for (auto id : deadUnits) world_.destroy(id);
    bool buildingsDestroyed = false;
    for (const auto& b : world_.footprints()) if (b.vitals().health == 0) {
        if (b.progression().hero && b.owner == player_.id) heroFallen_ = true;
        // Combat destruction never refunds crystals, including unfinished jobs.
        if (activePlayer(b.owner)) {
            auto& supply = mutableEconomy(b.owner).supply;
            supply.release(b.definition.cost.supply);
            for (const auto& job : b.productionState().production) supply.release(job.reservedSupply);
        }
        for (int y = 0; y < b.definition.height; ++y) for (int x = 0; x < b.definition.width; ++x)
            scenario_.map.release(b.origin + Cell{x, y});
        events_.emplace_back(BuildingDestroyed{b.id, b.owner});
        buildingsDestroyed = true;
    }
    if (buildingsDestroyed) {
        std::vector<EntityId> deadBuildings;
        for (const auto& b : world_.footprints()) if (b.vitals().health == 0) deadBuildings.push_back(b.id);
        for (auto id : deadBuildings) world_.destroy(id);
        for (auto& u : world_.circles()) if (u.orders().targetBuilding && !building(u.orders().targetBuilding)) {
            u.orders().targetBuilding = 0;
            if (u.orders().state == UnitState::ToHall) {
                u.motion().route.clear(); u.motion().next = 0;
                returnCargo(u);
            }
            else if (u.orders().state == UnitState::ToBuild || u.orders().state == UnitState::Building || u.orders().currentOrder.kind == OrderKind::UseAbility) issue(u, {OrderKind::Stop});
        }
    }
    if (deaths || buildingsDestroyed) {
        for (auto* entry : mutableEntities()) {
            auto& u = *entry;
            if (u.combat().attackTarget && !combatTarget(u.combat().attackTarget)) {
                resumeOrder(u);
            }
        }
        updateVision();
    }
}
}
