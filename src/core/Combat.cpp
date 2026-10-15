#include "CombatRules.hpp"
#include <algorithm>
#include <limits>

namespace rts {
void Simulation::cancelAttack(Unit& u) {
    if (u.combat().attackPhase == AttackPhase::Recovery) {
        u.combat().attackTicks += u.definition.attackCooldownTicks;
        u.combat().attackPhase = AttackPhase::Cooldown;
    } else if (u.combat().attackPhase == AttackPhase::Windup) {
        u.combat().attackTicks = 0; u.combat().attackPhase = AttackPhase::Ready;
    }
}
void Simulation::tickCombat() {
    tickRemains();
    std::vector<Hit> hits;
    tickProjectiles(hits); // Newly released shots start flying on the following tick.
    const auto combatants = mutableEntities();
    for (auto* combatant : combatants) {
        auto& u = *combatant;
        if (u.vitals().health <= 0 || !u.complete()) continue;
        if (u.combat().chaseTicks > 0) --u.combat().chaseTicks;
        if (u.combat().targetRetryTicks > 0) --u.combat().targetRetryTicks;
        if (u.combat().attackPhase == AttackPhase::Recovery || u.combat().attackPhase == AttackPhase::Cooldown) {
            if (u.combat().attackTicks > 0) --u.combat().attackTicks;
            if (u.combat().attackTicks == 0) {
                if (u.combat().attackPhase == AttackPhase::Recovery) {
                    u.combat().attackPhase = AttackPhase::Cooldown; u.combat().attackTicks = u.definition.attackCooldownTicks;
                }
                if (u.combat().attackTicks == 0) u.combat().attackPhase = AttackPhase::Ready;
            }
        }
        if (u.attackDamage() <= 0) continue;
        const bool ground = u.orders().currentOrder.kind == OrderKind::AttackGround;
        const bool automatic = !ground && u.orders().currentOrder.kind != OrderKind::Attack;
        if (automatic && u.combat().attackTarget && u.combat().attackTarget == u.combat().unreachableTarget && u.combat().targetRetryTicks > 0) {
            u.combat().attackTarget = 0; u.motion().route.clear(); u.motion().next = 0; u.orders().state = UnitState::Idle;
        }
        if (!u.combat().attackTarget && !ground && u.orders().state == UnitState::Attacking) resumeOrder(u);
        const bool scanning = u.orders().state == UnitState::Idle || u.orders().currentOrder.kind == OrderKind::AttackMove ||
            u.orders().currentOrder.kind == OrderKind::Patrol;
        const auto acquire = [&](bool onlyInReach) {
            const Unit* best = nullptr;
            float bestScore = std::numeric_limits<float>::max();
            const int vision = clock_.phase() == DayPhase::Day ? u.definition.dayVision : u.definition.nightVision;
            for (const auto* entry : combatants) {
                const auto& candidate = *entry;
                const auto candidateTarget = candidate.definition.collision == CollisionShape::Footprint ? CombatTarget{nullptr, &candidate} : CombatTarget{&candidate};
                if (!canAttack(u, candidateTarget, false) || (candidate.id == u.combat().unreachableTarget && u.combat().targetRetryTicks > 0)) continue;
                const bool inReach = attackReach(u, candidateTarget);
                if ((onlyInReach || !u.definition.mobile || u.orders().currentOrder.kind == OrderKind::Hold) && !inReach) continue;
                float score = groundLengthSquared(candidate.position - u.position);
                if (inReach) score -= 10000;
                else {
                    // Spread automatic melee acquisition along the front. A focused
                    // click never passes through this target replacement policy.
                    for (const auto& ally : world_.circles()) if (ally.id != u.id && ally.owner == u.owner &&
                        ally.combat().attackTarget == candidate.id && !ally.definition.projectile) score += 2;
                    if (candidate.id == u.combat().attackTarget && u.motion().blockedTicks >= 18) score += 8;
                }
                if (score < bestScore && visionReaches(map(), {u.cell, vision, airborne(u.definition.movement)}, candidate.cell)) {
                    best = &candidate; bestScore = score;
                }
            }
            return best;
        };
        if (!u.combat().attackTarget && !ground && scanning) {
            const auto* best = acquire(false);
            if (best) { u.combat().attackTarget = best->id; chase(u, combatTarget(best->id)); }
        } else if (automatic && u.combat().attackTarget && u.combat().attackPhase != AttackPhase::Windup && u.combat().attackPhase != AttackPhase::Recovery &&
            (clock_.elapsedTicks() + u.id) % 6 == 0) {
            const auto current = combatTarget(u.combat().attackTarget);
            if (current && !attackReach(u, current)) {
                const auto* best = acquire(u.motion().blockedTicks < 18);
                if (best && best->id != u.combat().attackTarget) {
                    u.combat().attackTarget = best->id; u.motion().route.clear(); u.motion().next = 0; u.motion().blockedTicks = 0;
                    chase(u, combatTarget(best->id));
                }
            }
        }
        if (!u.combat().attackTarget && !ground) continue;
        const auto target = combatTarget(u.combat().attackTarget);
        if (!ground && (!canAttack(u, target, !automatic && u.orders().currentOrder.forceAttack) || !targetVisible(u, target) ||
            (u.orders().currentOrder.kind == OrderKind::Hold && !attackReach(u, target)))) {
            resumeOrder(u);
            continue;
        }
        const bool inReach = (ground ? groundAttackReach(u, u.orders().currentOrder.cell) : attackReach(u, target));
        const auto release = [&] { if (ground) releaseGroundAttack(u); else releaseAttack(u, target, hits); };
        if (u.combat().attackPhase == AttackPhase::Windup) {
            if (--u.combat().attackTicks <= 0) {
                if (inReach) release();
                combat::recover(u);
            }
            continue;
        }
        if (u.combat().attackPhase == AttackPhase::Recovery) continue;
        if (inReach) {
            u.motion().route.clear(); u.motion().next = 0; u.orders().state = UnitState::Attacking;
            const auto delta = (ground ? center(u.orders().currentOrder.cell) : attackAim(target, u.position)) - u.position;
            u.facing = {delta.x > .2f ? 1 : delta.x < -.2f ? -1 : 0, delta.y > .2f ? 1 : delta.y < -.2f ? -1 : 0};
            if (u.combat().attackPhase == AttackPhase::Ready) {
                u.combat().attackPhase = AttackPhase::Windup; u.combat().attackTicks = u.definition.attackWindupTicks;
                if (u.combat().attackTicks == 0) { release(); combat::recover(u); }
            }
        } else if (u.combat().chaseTicks == 0) {
            if (ground) chaseGround(u); else chase(u, target);
        }
    }
    resolveHits(hits);
}
}
