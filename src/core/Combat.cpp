#include "CombatRules.hpp"
#include <algorithm>
#include <limits>

namespace rts {
void Simulation::cancelAttack(Unit& u) {
    if (u.attackPhase == AttackPhase::Recovery) {
        u.attackTicks += u.definition.attackCooldownTicks;
        u.attackPhase = AttackPhase::Cooldown;
    } else if (u.attackPhase == AttackPhase::Windup) {
        u.attackTicks = 0; u.attackPhase = AttackPhase::Ready;
    }
}
void Simulation::tickCombat() {
    for (auto& corpse : corpses_) --corpse.remainingTicks;
    std::erase_if(corpses_, [](const Corpse& corpse) { return corpse.remainingTicks <= 0; });
    std::vector<Hit> hits;
    tickProjectiles(hits); // Newly released shots start flying on the following tick.
    for (auto& u : units_) {
        if (u.chaseTicks > 0) --u.chaseTicks;
        if (u.attackPhase == AttackPhase::Recovery || u.attackPhase == AttackPhase::Cooldown) {
            if (u.attackTicks > 0) --u.attackTicks;
            if (u.attackTicks == 0) {
                if (u.attackPhase == AttackPhase::Recovery) {
                    u.attackPhase = AttackPhase::Cooldown; u.attackTicks = u.definition.attackCooldownTicks;
                }
                if (u.attackTicks == 0) u.attackPhase = AttackPhase::Ready;
            }
        }
        if (u.attackDamage() <= 0) continue;
        if (!u.targetUnit && u.state == UnitState::Idle && !u.pendingOrder) {
            const Unit* best = nullptr;
            int bestDistance = std::numeric_limits<int>::max();
            const int vision = clock_.phase() == DayPhase::Day ? u.definition.dayVision : u.definition.nightVision;
            for (const auto& candidate : units_) {
                if (!canAttack(u, candidate)) continue;
                const Cell d = candidate.cell - u.cell;
                const int distance = d.x * d.x + d.y * d.y;
                if (distance < bestDistance && visionReaches(map(), {u.cell, vision, airborne(u.definition.movement)}, candidate.cell)) {
                    best = &candidate; bestDistance = distance;
                }
            }
            if (best) applyOrder(u, {OrderKind::Attack, {}, best->id});
        }
        if (!u.targetUnit) continue;
        const auto* target = unit(u.targetUnit);
        if (!target || !canAttack(u, *target) || !targetVisible(u, *target)) {
            u.targetUnit = 0;
            if (!u.pendingOrder) issue(u, {OrderKind::Stop});
            else cancelAttack(u);
            continue;
        }
        const bool inReach = u.progress == 0 && attackReach(u, *target);
        if (u.attackPhase == AttackPhase::Windup) {
            if (--u.attackTicks <= 0) {
                if (inReach) releaseAttack(u, *target, hits);
                combat::recover(u);
            }
            continue;
        }
        if (u.attackPhase == AttackPhase::Recovery) continue;
        if (inReach) {
            u.route.clear(); u.next = 0; u.state = UnitState::Attacking;
            const auto delta = target->position - u.position;
            u.facing = {delta.x > .2f ? 1 : delta.x < -.2f ? -1 : 0, delta.y > .2f ? 1 : delta.y < -.2f ? -1 : 0};
            if (u.attackPhase == AttackPhase::Ready) {
                u.attackPhase = AttackPhase::Windup; u.attackTicks = u.definition.attackWindupTicks;
                if (u.attackTicks == 0) { releaseAttack(u, *target, hits); combat::recover(u); }
            }
        } else if (u.progress == 0 && u.chaseTicks == 0) chase(u, *target);
    }
    resolveHits(hits);
}
}
