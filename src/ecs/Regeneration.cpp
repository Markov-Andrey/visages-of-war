#include "rts/ecs/Systems.hpp"
#include <entt/entity/registry.hpp>
#include <algorithm>

namespace rts::ecs {
bool regenerateVitals(World& world, std::uint64_t tick) {
    bool deaths = false;
    for (auto&& [handle, entity, vitals] : world.registry().view<Entity, components::Vitals>().each()) {
        if (vitals.health <= 0 || ++vitals.regenerationTicks < simulationTicksPerSecond) continue;
        vitals.regenerationTicks = 0;
        const auto before = vitals.health;
        vitals.health = std::clamp(vitals.health + Vital::fromHundredths(entity.definition.healthRegenHundredths), Vital{}, entity.maximumHealth());
        vitals.mana = std::clamp(vitals.mana + Vital::fromHundredths(entity.definition.manaRegenHundredths), Vital{}, entity.maximumMana());
        vitals.healthFeedback.record(before, vitals.health, tick);
        deaths |= vitals.health == 0;
    }
    return deaths;
}
}
