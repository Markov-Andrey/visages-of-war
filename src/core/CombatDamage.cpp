#include "CombatRules.hpp"
#include <algorithm>

namespace rts {
void Simulation::resolveHits(const std::vector<Hit>& hits) {
    // Resolve this tick's hits together, so equal duels do not favour the lower entity ID.
    std::vector<std::pair<EntityId, PlayerId>> kills;
    for (const auto hit : hits) if (auto* target = mutableUnit(hit.target); target && target->health > 0) {
        target->health = std::max(0, target->health - hit.damage);
        if (target->health == 0) kills.emplace_back(target->id, hit.owner);
    }
    // Dead heroes cannot gain levels or heal out of a simultaneous lethal hit.
    for (const auto [victim, killer] : kills) rewardKill(*unit(victim), killer);
    bool deaths = false;
    for (const auto& u : units_) if (u.health == 0) {
        if (u.hero && u.owner == player_.id) heroFallen_ = true;
        if (u.owner == player_.id) supply_.release(u.definition.cost.supply);
        const Cell cell{int(std::floor(u.position.x)), int(std::floor(u.position.y))};
        corpses_.push_back({u.position, cell, map().surfaceHeight(cell, u.position), u.owner, 90, u.definition.sprite});
        events_.emplace_back(UnitDied{u.id, u.owner});
        deaths = true;
    }
    std::erase_if(units_, [](const Unit& u) { return u.health == 0; });
    if (deaths) {
        for (auto& u : units_) {
            if (u.pendingOrder && u.pendingOrder->kind == OrderKind::Attack && !unit(u.pendingOrder->target)) u.pendingOrder.reset();
            if (u.targetUnit && !unit(u.targetUnit)) {
                resumeOrder(u);
            }
        }
        updateVision();
    }
}
}
