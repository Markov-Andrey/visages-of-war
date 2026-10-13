#include "rts/Simulation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rts {
void Simulation::tickRepair(Unit& u) {
    u.abilityWorking = false;
    const auto* ability = u.ability(u.currentOrder.abilityId);
    if (!ability || !ability->repair || !canTargetAbility(u, *ability, u.currentOrder.target)) {
        issue(u, {OrderKind::Stop}); return;
    }
    auto* recipient = mutableEntity(u.currentOrder.target);
    if (!recipient || !abilityReach(u, *ability, u.currentOrder.target, u.position)) return;
    const auto& definition = recipient->definition;
    auto& health = recipient->health;
    auto& credit = recipient->repairCredit;
    const auto& repair = *ability->repair;
    const int productionTicks = definition.constructible ? definition.constructionTicks : definition.trainingTicks;
    const std::int64_t duration = std::max<std::int64_t>(1, (std::int64_t(productionTicks) * repair.constructionTimePercent + 99) / 100);
    const std::int64_t maximum = recipient->maximumHealth();
    const std::int64_t denominator = maximum * 100;
    const std::int64_t pricePerHealth = std::int64_t(definition.cost.crystals) * repair.constructionCostPercent;
    auto& balance = mutableEconomy(u.owner).crystals;
    if (pricePerHealth > credit && balance == 0) return; // Wait without banking repair work or playing work animation.
    u.abilityWorking = true;
    u.abilityAnimation.sustain(*ability, u.definition);
    const Vec2 delta = recipient->position - u.position;
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
