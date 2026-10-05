#include "rts/Simulation.hpp"

namespace rts {
bool Simulation::activateAbility(EntityId id, int abilityId) {
    auto* unit = mutableUnit(id);
    if (!unit || unit->health <= 0 || unit->owner != player_.id) return false;
    const auto* ability = unit->ability(abilityId);
    if (!ability) return false;
    if (unit->abilityCooldown(abilityId) > 0) { setMessage(L"Способность ещё перезаряжается."); return false; }
    if (unit->mana < ability->manaCost) { setMessage(L"Недостаточно маны."); return false; }
    unit->mana -= ability->manaCost;
    unit->abilityCooldowns.push_back({abilityId, ability->cooldownTicks});
    setMessage({});
    // The current ability is an untargeted placeholder: no effect or change of movement order.
    return true;
}
}
