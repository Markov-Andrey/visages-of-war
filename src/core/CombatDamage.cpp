#include "CombatRules.hpp"
#include <algorithm>

namespace rts {
void Simulation::resolveHits(const std::vector<Hit>& hits) {
    // Resolve this tick's hits together, so equal duels do not favour the lower entity ID.
    std::vector<std::pair<EntityId, PlayerId>> kills;
    for (const auto hit : hits) {
        if (auto* target = mutableEntity(hit.target); target && target->health > 0) {
            const auto before = target->health;
            target->health = std::max(Vital{}, target->health - armorRules_.damage(hit.damage, target->armor()));
            target->healthFeedback.record(before, target->health, clock_.elapsedTicks());
            if (target->health == 0) kills.emplace_back(target->id, hit.owner);
        }
    }
    // Dead heroes cannot gain levels or heal out of a simultaneous lethal hit.
    for (const auto [victim, killer] : kills) rewardKill(*entity(victim), killer);
    removeDeadEntities();
}
void Simulation::removeDeadEntities() {
    bool deaths = false;
    for (const auto& u : units_) if (u.health == 0) {
        if (u.hero && u.owner == player_.id) heroFallen_ = true;
        if (activePlayer(u.owner)) {
            auto& supply = mutableEconomy(u.owner).supply;
            supply.release(u.definition.cost.supply);
            for (const auto& job : u.production) supply.release(job.reservedSupply);
        }
        leaveRemains(u);
        events_.emplace_back(UnitDied{u.id, u.owner});
        deaths = true;
    }
    std::erase_if(units_, [](const Unit& u) { return u.health == 0; });
    bool buildingsDestroyed = false;
    for (const auto& b : buildings_) if (b.health == 0) {
        if (b.hero && b.owner == player_.id) heroFallen_ = true;
        // Combat destruction never refunds crystals, including unfinished jobs.
        if (activePlayer(b.owner)) {
            auto& supply = mutableEconomy(b.owner).supply;
            supply.release(b.definition.cost.supply);
            for (const auto& job : b.production) supply.release(job.reservedSupply);
        }
        for (int y = 0; y < b.definition.height; ++y) for (int x = 0; x < b.definition.width; ++x)
            scenario_.map.release(b.origin + Cell{x, y});
        events_.emplace_back(BuildingDestroyed{b.id, b.owner});
        buildingsDestroyed = true;
    }
    if (buildingsDestroyed) {
        std::erase_if(buildings_, [](const Building& b) { return b.health == 0; });
        for (auto& u : units_) if (u.targetBuilding && !building(u.targetBuilding)) {
            u.targetBuilding = 0;
            if (u.state == UnitState::ToHall) {
                u.route.clear(); u.next = 0;
                returnCargo(u);
            }
            else if (u.state == UnitState::ToBuild || u.state == UnitState::Building || u.currentOrder.kind == OrderKind::UseAbility) issue(u, {OrderKind::Stop});
        }
    }
    if (deaths || buildingsDestroyed) {
        for (auto* entry : mutableEntities()) {
            auto& u = *entry;
            if (u.attackTarget && !combatTarget(u.attackTarget)) {
                resumeOrder(u);
            }
        }
        updateVision();
    }
}
}
