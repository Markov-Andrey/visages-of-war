#include "rts/Simulation.hpp"
#include <algorithm>
#include <cstdint>

namespace rts {
namespace {
void regenerate(Vital& value, Vital maximum, int rate) {
    value = std::clamp(value + Vital::fromHundredths(rate), Vital{}, maximum);
}

}
void Simulation::tickRegeneration() {
    bool deaths = false;
    const auto apply = [&](Entity& entity) {
        if (entity.health <= 0) return;
        if (++entity.regenerationTicks < ticksPerSecond) return;
        entity.regenerationTicks = 0;
        const auto before = entity.health;
        regenerate(entity.health, entity.maximumHealth(), entity.definition.healthRegenHundredths);
        regenerate(entity.mana, entity.maximumMana(), entity.definition.manaRegenHundredths);
        entity.healthFeedback.record(before, entity.health, clock_.elapsedTicks());
        deaths |= entity.health == 0;
    };
    for (auto& entity : units_) apply(entity);
    for (auto& entity : buildings_) apply(entity);
    // Decay ignores armor and grants no kill reward, but uses normal death cleanup.
    if (deaths) removeDeadEntities();
}
}
