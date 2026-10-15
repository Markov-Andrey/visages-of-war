#include "rts/ecs/Systems.hpp"
#include <entt/entity/registry.hpp>

namespace rts::ecs {
void advanceAbilityTimers(World& world) {
    for (auto&& [handle, casting] : world.registry().view<components::Abilities>().each()) {
        casting.abilityAnimation.beginTick();
        for (auto& cooldown : casting.abilityCooldowns) --cooldown.remainingTicks;
        std::erase_if(casting.abilityCooldowns, [](const auto& cooldown) { return cooldown.remainingTicks <= 0; });
    }
}
}
