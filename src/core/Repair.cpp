#include "rts/Simulation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rts {
void Simulation::tickRepair(Entity& u, const AbilityDefinition& ability, const RepairDefinition& repair, EntityId target, AbilityEffectState& state) {
    auto* recipient = mutableEntity(target);
    if (!recipient || recipient->vitals().health >= recipient->maximumHealth() || !abilityReach(u, ability, target, u.position)) return;
    const auto& definition = recipient->definition;
    auto& health = recipient->vitals().health;
    auto& credit = recipient->vitals().repairCredit;
    const int productionTicks = definition.constructible ? definition.constructionTicks : definition.trainingTicks;
    const std::int64_t duration = std::max<std::int64_t>(1, (std::int64_t(productionTicks) * repair.constructionTimePercent + 99) / 100);
    const std::int64_t maximum = recipient->maximumHealth().hundredths();
    const std::int64_t denominator = maximum * 100;
    const std::int64_t pricePerHealth = std::int64_t(definition.cost.crystals) * repair.constructionCostPercent;
    auto& balance = mutableEconomy(u.owner).crystals;
    if (pricePerHealth > credit && balance == 0) return; // Wait without banking repair work or playing work animation.
    u.casting().abilityWorking = true;
    u.casting().abilityAnimation.sustain(ability, u.definition);
    const Vec2 delta = recipient->position - u.position;
    const Cell facing{delta.x > .2f ? 1 : delta.x < -.2f ? -1 : 0, delta.y > .2f ? 1 : delta.y < -.2f ? -1 : 0};
    if (facing != Cell{}) u.facing = facing;
    state.progress += maximum;
    std::int64_t restored = std::min(state.progress / duration, maximum - health.hundredths());
    state.progress %= duration;
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
    if (!restored) { u.casting().abilityWorking = false; return; }
    balance -= charge;
    credit += charge * denominator - restored * pricePerHealth;
    health += Vital::fromHundredths(restored);
}
}
