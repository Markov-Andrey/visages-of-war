#include "rts/Simulation.hpp"

namespace rts {
bool Simulation::activateAbility(EntityId id, int abilityId, EntityId target) {
    return activateAbilityFor(player_.id, id, abilityId, target);
}
bool Simulation::activateAbilityFor(PlayerId actor, EntityId id, int abilityId, EntityId target) {
    auto* unit = mutableUnit(id);
    if (!activePlayer(actor) || !unit || unit->health <= 0 || unit->owner != actor) return false;
    const auto* ability = unit->ability(abilityId);
    if (!ability) return false;
    if (unit->abilityCooldown(abilityId) > 0) { setMessage(L"Способность ещё перезаряжается."); return false; }
    if (unit->mana < ability->manaCost) { setMessage(L"Недостаточно маны."); return false; }
    if (!canTargetAbility(*unit, *ability, target)) { setMessage(L"Способность нельзя применить к этой цели."); return false; }
    if (ability->repair) {
        if (!startRepair(*unit, *ability, target, false)) { setMessage(L"Нет доступного пути для ремонта."); return false; }
        setMessage({}); return true;
    }
    unit->mana -= ability->manaCost;
    unit->abilityCooldowns.push_back({abilityId, ability->cooldownTicks});
    setMessage({});
    // Untargeted placeholders preserve the current movement order.
    return true;
}
bool Simulation::setAutocast(EntityId id, int abilityId, bool enabled) {
    return setAutocastFor(player_.id, id, abilityId, enabled);
}
bool Simulation::setAutocastFor(PlayerId actor, EntityId id, int abilityId, bool enabled) {
    auto* u = mutableUnit(id);
    if (!activePlayer(actor) || !u || u->health <= 0 || u->owner != actor) return false;
    const auto* ability = u->ability(abilityId);
    if (!ability || !ability->autocast) return false;
    if (enabled && !u->autocastEnabled(abilityId)) u->autocastAbilities.push_back(abilityId);
    if (!enabled) {
        std::erase(u->autocastAbilities, abilityId);
        if (u->automaticAbility && u->currentOrder.abilityId == abilityId) issue(*u, {OrderKind::Stop});
    }
    return true;
}
bool Simulation::canTargetAbility(const Unit& u, const AbilityDefinition& ability, EntityId target) const {
    if (u.health <= 0 || !activePlayer(u.owner)) return false;
    if (ability.targeting == AbilityTargeting::None) return target == 0;
    if (!ability.repair) return false;
    if (const auto* recipient = unit(target)) {
        return (ability.targeting == AbilityTargeting::FriendlyUnit || ability.targeting == AbilityTargeting::FriendlyEntity) &&
            recipient->owner == u.owner && recipient->health > 0 && recipient->health < recipient->maximumHealth() &&
            visibleTo(u.owner, recipient->cell);
    }
    if (ability.targeting != AbilityTargeting::FriendlyBuilding && ability.targeting != AbilityTargeting::FriendlyEntity) return false;
    const auto* b = building(target);
    if (!b || b->owner != u.owner || b->health <= 0 || !b->complete() || b->health >= b->definition.maximumHealth) return false;
    for (int y = 0; y < b->definition.height; ++y) for (int x = 0; x < b->definition.width; ++x)
        if (visibleTo(u.owner, b->origin + Cell{x, y})) return true;
    return false;
}
const AbilityDefinition* Simulation::contextualAbility(const Unit& u, EntityId target) const {
    for (const auto& ability : u.definition.abilities)
        if (ability.rightClick && canTargetAbility(u, ability, target) && !u.abilityCooldown(ability.id) && u.mana >= ability.manaCost)
            return &ability;
    return nullptr;
}
}
