#pragma once
#include "rts/Simulation.hpp"

namespace rts {
inline const Crystal* harvestEffectTarget(const Simulation& game, const Unit& unit) {
    if (unit.health <= 0 || unit.state != UnitState::Harvesting || unit.next < unit.route.size() ||
        !unit.definition.canGather() || !unit.definition.harvesting().effect || !game.fog().visible(unit.cell) ||
        unit.targetCrystal < 0 || size_t(unit.targetCrystal) >= game.crystals().size()) return nullptr;
    const auto& crystal = game.crystals()[unit.targetCrystal];
    return crystal.remaining > 0 && game.crystalVisible(crystal) ? &crystal : nullptr;
}
}
