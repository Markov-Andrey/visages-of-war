#include "CombatRules.hpp"
#include <algorithm>

namespace rts {
void Simulation::resolveHits(const std::vector<Hit>& hits) {
    // Resolve this tick's hits together, so equal duels do not favour the lower entity ID.
    std::vector<std::pair<EntityId, PlayerId>> kills;
    for (const auto hit : hits) {
        if (auto* target = mutableUnit(hit.target); target && target->health > 0) {
            const int before = target->health;
            target->health = std::max(0, target->health - hit.damage);
            target->healthFeedback.record(before, target->health, clock_.elapsedTicks());
            if (target->health == 0) kills.emplace_back(target->id, hit.owner);
        } else if (auto* building = mutableBuilding(hit.target); building && building->health > 0) {
            building->health = std::max(0, building->health - hit.damage);
        }
    }
    // Dead heroes cannot gain levels or heal out of a simultaneous lethal hit.
    for (const auto [victim, killer] : kills) rewardKill(*unit(victim), killer);
    bool deaths = false;
    for (const auto& u : units_) if (u.health == 0) {
        if (u.hero && u.owner == player_.id) heroFallen_ = true;
        if (u.owner == player_.id) supply_.release(u.definition.cost.supply);
        leaveRemains(u);
        events_.emplace_back(UnitDied{u.id, u.owner});
        deaths = true;
    }
    std::erase_if(units_, [](const Unit& u) { return u.health == 0; });
    bool buildingsDestroyed = false;
    for (const auto& b : buildings_) if (b.health == 0) {
        // Combat destruction never refunds crystals, including unfinished jobs.
        if (b.owner == player_.id) {
            supply_.release(b.definition.cost.supply);
            for (const auto& job : b.production) supply_.release(job.reservedSupply);
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
            else if (u.state == UnitState::ToBuild || u.state == UnitState::Building) issue(u, {OrderKind::Stop});
        }
    }
    if (deaths) {
        for (auto& u : units_) {
            if (u.targetUnit && !unit(u.targetUnit)) {
                resumeOrder(u);
            }
        }
        updateVision();
    }
    else if (buildingsDestroyed) updateVision();
}
}
