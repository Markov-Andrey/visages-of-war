#include "rts/Simulation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rts {
std::vector<Cell> Simulation::repairPositions(const Unit& u, EntityId target) const {
    if (const auto* recipient = unit(target)) {
        std::vector<Cell> positions;
        const float reach = u.definition.collisionRadius + recipient->definition.collisionRadius + .75f;
        const int extent = static_cast<int>(std::ceil(reach * 2));
        for (int y = -extent; y <= extent; ++y) for (int x = -extent; x <= extent; ++x) {
            const Cell c = recipient->cell + Cell{x, y};
            if (map().walkable(c, u.definition.movement) && repairReach(u, target, center(c))) positions.push_back(c);
        }
        return positions;
    }
    const auto* b = building(target);
    if (!b) return {};
    auto positions = perimeter(map(), b->origin, b->definition.width, b->definition.height, u.definition.movement);
    // A worker cannot repair through a cliff from the lower side of the footprint.
    if (!airborne(u.definition.movement)) std::erase_if(positions, [&](Cell c) {
        return map().at(c).height != map().at(b->origin).height;
    });
    return positions;
}
bool Simulation::repairReach(const Unit& u, EntityId target, Vec2 position) const {
    if (const auto* recipient = unit(target)) {
        const float contact = u.definition.collisionRadius + recipient->definition.collisionRadius;
        const float distance = groundLength(position - recipient->position);
        if (distance > contact + .75f || (u.id != target && distance < contact)) return false;
        return map().canTraverse(position, recipient->position, .001f, u.definition.movement);
    }
    const auto positions = repairPositions(u, target);
    return std::any_of(positions.begin(), positions.end(), [&](Cell c) {
        return groundLengthSquared(position - center(c)) <= .0225f &&
            map().canTraverse(position, center(c), u.definition.collisionRadius, u.definition.movement);
    });
}
void Simulation::approachRepair(Unit& u) {
    u.chaseTicks = 15 + int(u.id % 7);
    u.abilityWorking = false;
    if (u.next < u.route.size() && repairReach(u, u.currentOrder.target, u.routeDestination)) return;
    if (combatSearchBudget_ <= 0) { u.chaseTicks = 1; u.state = UnitState::ToAbility; return; }
    --combatSearchBudget_;
    const auto positions = repairPositions(u, u.currentOrder.target);
    if (auto path = findUnitPath(map(), u.position, positions, unitObstacles(u, true, true),
        u.definition.collisionRadius, u.definition.movement, 2048)) {
        followPath(u, std::move(*path), UnitState::ToAbility);
    } else {
        u.route.clear(); u.next = 0; u.state = UnitState::ToAbility;
    }
}
bool Simulation::startRepair(Unit& u, const AbilityDefinition& ability, EntityId target, bool automatic) {
    if (!ability.repair || !canTargetAbility(u, ability, target) || u.abilityCooldown(ability.id) || u.mana < ability.manaCost) return false;
    const auto positions = repairPositions(u, target);
    if (positions.empty()) return false;
    auto path = repairReach(u, target, u.position) ? std::optional<Path>{Path{{u.cell}, 0, u.position}} :
        findUnitPath(map(), u.position, positions, unitObstacles(u, true, true),
            u.definition.collisionRadius, u.definition.movement, automatic ? 2048 : 0);
    if (!path) path = findUnitPath(map(), u.position, positions, {}, u.definition.collisionRadius, u.definition.movement, automatic ? 2048 : 0);
    if (!path) return false;
    Order order{OrderKind::UseAbility, building(target) ? building(target)->origin : unit(target)->cell, target}; order.abilityId = ability.id;
    applyOrder(u, order, std::move(path));
    u.automaticAbility = automatic;
    u.chaseTicks = 15 + int(u.id % 7);
    if (repairReach(u, target, u.position)) { u.route.clear(); u.next = 0; u.state = UnitState::UsingAbility; }
    u.mana -= ability.manaCost;
    if (ability.cooldownTicks) u.abilityCooldowns.push_back({ability.id, ability.cooldownTicks});
    return true;
}
void Simulation::tickAutocast(Unit& u) {
    if (u.health <= 0 || !activePlayer(u.owner) || u.state != UnitState::Idle || u.currentOrder.kind != OrderKind::Stop ||
        u.next < u.route.size() || u.attackTarget) return;
    for (const auto& ability : u.definition.abilities) {
        if (!ability.autocast || !ability.repair || !u.autocastEnabled(ability.id) ||
            ability.autocast->condition != AutocastCondition::Stopped || u.abilityCooldown(ability.id) || u.mana < ability.manaCost) continue;
        const auto& automatic = *ability.autocast;
        // Stagger idle scans by stable unit ID rather than searching for the whole army on one tick.
        if ((clock_.elapsedTicks() + u.id) % automatic.scanIntervalTicks) continue;
        std::vector<std::pair<float, EntityId>> candidates;
        const auto consider = [&](EntityId id, const EntityDefinition& definition, std::int64_t credit) {
            if (!canTargetAbility(u, ability, id)) return;
            if (ability.repair->constructionCostPercent > 0 && definition.cost.crystals > 0 &&
                economy(u.owner).crystals == 0 && credit < std::int64_t(definition.cost.crystals) * ability.repair->constructionCostPercent) return;
            float distance = std::numeric_limits<float>::max();
            if (repairReach(u, id, u.position)) distance = 0;
            else for (Cell c : repairPositions(u, id)) distance = std::min(distance, groundLengthSquared(center(c) - u.position));
            if (distance <= automatic.radius * automatic.radius) candidates.emplace_back(distance, id);
        };
        for (const auto& b : buildings_) consider(b.id, b.definition, b.repairCredit);
        if (ability.targeting != AbilityTargeting::FriendlyBuilding)
            for (const auto& recipient : units_) consider(recipient.id, recipient.definition, recipient.repairCredit);
        std::sort(candidates.begin(), candidates.end());
        for (const auto& [distance, target] : candidates)
            if (startRepair(u, ability, target, true)) return;
    }
}
void Simulation::tickRepair(Unit& u) {
    u.abilityWorking = false;
    const auto* ability = u.ability(u.currentOrder.abilityId);
    if (!ability || !ability->repair || !canTargetAbility(u, *ability, u.currentOrder.target)) {
        issue(u, {OrderKind::Stop}); return;
    }
    auto* b = mutableBuilding(u.currentOrder.target);
    auto* recipient = b ? nullptr : mutableUnit(u.currentOrder.target);
    if (!repairReach(u, u.currentOrder.target, u.position)) {
        if (recipient) { u.abilityWorking = false; return; }
        issue(u, {OrderKind::Stop}); return;
    }
    const auto& definition = b ? b->definition : recipient->definition;
    auto& health = b ? b->health : recipient->health;
    auto& credit = b ? b->repairCredit : recipient->repairCredit;
    const auto& repair = *ability->repair;
    const int productionTicks = b ? definition.constructionTicks : definition.trainingTicks;
    const std::int64_t duration = std::max<std::int64_t>(1, (std::int64_t(productionTicks) * repair.constructionTimePercent + 99) / 100);
    const std::int64_t maximum = b ? definition.maximumHealth : recipient->maximumHealth();
    const std::int64_t denominator = maximum * 100;
    const std::int64_t pricePerHealth = std::int64_t(definition.cost.crystals) * repair.constructionCostPercent;
    auto& balance = mutableEconomy(u.owner).crystals;
    if (pricePerHealth > credit && balance == 0) return; // Wait without banking repair work or playing work animation.
    u.abilityWorking = true;
    const Vec2 delta = (b ? Vec2{b->origin.x + definition.width*.5f, b->origin.y + definition.height*.5f} : recipient->position) - u.position;
    const Cell facing{delta.x > .2f ? 1 : delta.x < -.2f ? -1 : 0, delta.y > .2f ? 1 : delta.y < -.2f ? -1 : 0};
    if (facing != Cell{}) u.facing = facing;
    u.repairProgress += maximum;
    std::int64_t restored = std::min(u.repairProgress / duration, maximum - health);
    u.repairProgress %= duration;
    if (restored <= 0) return;
    const auto chargeFor = [&](std::int64_t health) {
        return (std::max<std::int64_t>(0, health * pricePerHealth - credit) + denominator - 1) / denominator;
    };
    auto charge = chargeFor(restored);
    if (charge > balance) {
        // Here balance < chargeFor(restored), so this multiplication is bounded by the repair's price.
        restored = (balance * denominator + credit) / pricePerHealth;
        charge = chargeFor(restored);
    }
    if (!restored) { u.abilityWorking = false; return; }
    balance -= charge;
    credit += charge * denominator - restored * pricePerHealth;
    health += static_cast<int>(restored);
    if (health == maximum) issue(u, {OrderKind::Stop});
}
}
