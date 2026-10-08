#include "CombatRules.hpp"
#include <algorithm>
#include <cmath>

namespace rts {
bool Simulation::targetVisible(const Unit& observer, const Unit& target) const {
    if (observer.owner == player_.id) return fog_.visible(target.cell);
    const bool day = clock_.phase() == DayPhase::Day;
    for (const auto& ally : units_) if (ally.owner == observer.owner && ally.health > 0 &&
        visionReaches(map(), {ally.cell, day ? ally.definition.dayVision : ally.definition.nightVision,
            airborne(ally.definition.movement)}, target.cell)) return true;
    return false;
}
bool Simulation::canAttack(const Unit& attacker, const Unit& target) const {
    return attacker.attackDamage() > 0 && target.health > 0 && combat::hostile(attacker.owner, target.owner) &&
        combat::accepts(attacker.definition.attackTargets, airborne(attacker.definition.movement), airborne(target.definition.movement));
}
bool Simulation::attackReach(const Unit& attacker, const Unit& target, std::optional<Vec2> origin) const {
    if (!canAttack(attacker, target)) return false;
    const Vec2 start = origin.value_or(attacker.position);
    const Vec2 delta = target.position - start;
    if (groundLengthSquared(delta) > attacker.definition.attackRange * attacker.definition.attackRange) return false;
    if (attacker.definition.projectile) return true;
    // Direct melee cannot reach across a cliff or through a static obstacle.
    const Cell from = cellAt(start), d = target.cell - from;
    const int steps = std::max(std::abs(d.x), std::abs(d.y));
    if (steps == 0) return map().walkable(from, attacker.definition.movement);
    Cell previous = from;
    for (int i = 1; i <= steps; ++i) {
        const Cell next{from.x + int(std::lround(float(d.x) * i / steps)), from.y + int(std::lround(float(d.y) * i / steps))};
        if (!map().canStep(previous, next, attacker.definition.movement)) return false;
        previous = next;
    }
    return true;
}
bool Simulation::attack(std::span<const EntityId> ids, EntityId targetId) {
    const auto* target = unit(targetId);
    if (!target || !combat::hostile(player_.id, target->owner) || !fog_.visible(target->cell)) return false;
    bool any = false;
    for (EntityId id : ids) {
        auto* u = mutableUnit(id);
        if (!u || u->owner != player_.id || !canAttack(*u, *target)) continue;
        issue(*u, {OrderKind::Attack, {}, targetId}); any = true;
    }
    setMessage(any ? L"Атакуем выбранную цель." : L"Выбранные юниты не могут атаковать эту цель.");
    return any;
}
void Simulation::chase(Unit& u, const Unit& target) {
    if (attackReach(u, target)) {
        u.route.clear(); u.next = 0; u.blockedTicks = 0; u.state = UnitState::Attacking;
        return;
    }
    approachCombat(u, target.position, &target);
}
}
