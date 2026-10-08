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
    tickRemains();
    std::vector<Hit> hits;
    tickProjectiles(hits); // Newly released shots start flying on the following tick.
    for (auto& u : units_) {
        if (u.chaseTicks > 0) --u.chaseTicks;
        if (u.targetRetryTicks > 0) --u.targetRetryTicks;
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
        const bool ground = u.currentOrder.kind == OrderKind::AttackGround;
        const bool automatic = !ground && u.currentOrder.kind != OrderKind::Attack;
        if (automatic && u.targetUnit && u.targetUnit == u.unreachableTarget && u.targetRetryTicks > 0) {
            u.targetUnit = 0; u.route.clear(); u.next = 0; u.state = UnitState::Idle;
        }
        if (!u.targetUnit && !ground && u.state == UnitState::Attacking) resumeOrder(u);
        const bool scanning = u.state == UnitState::Idle || u.currentOrder.kind == OrderKind::AttackMove ||
            u.currentOrder.kind == OrderKind::Patrol;
        const auto acquire = [&](bool onlyInReach) {
            const Unit* best = nullptr;
            float bestScore = std::numeric_limits<float>::max();
            const int vision = clock_.phase() == DayPhase::Day ? u.definition.dayVision : u.definition.nightVision;
            for (const auto& candidate : units_) {
                if (!canAttack(u, candidate) || (candidate.id == u.unreachableTarget && u.targetRetryTicks > 0)) continue;
                const bool inReach = attackReach(u, candidate);
                if ((onlyInReach || u.currentOrder.kind == OrderKind::Hold) && !inReach) continue;
                float score = groundLengthSquared(candidate.position - u.position);
                if (inReach) score -= 10000;
                else {
                    // Spread automatic melee acquisition along the front. A focused
                    // click never passes through this target replacement policy.
                    for (const auto& ally : units_) if (ally.id != u.id && ally.owner == u.owner &&
                        ally.targetUnit == candidate.id && !ally.definition.projectile) score += 2;
                    if (candidate.id == u.targetUnit && u.blockedTicks >= 18) score += 8;
                }
                if (score < bestScore && visionReaches(map(), {u.cell, vision, airborne(u.definition.movement)}, candidate.cell)) {
                    best = &candidate; bestScore = score;
                }
            }
            return best;
        };
        if (!u.targetUnit && !ground && scanning) {
            const auto* best = acquire(false);
            if (best) { u.targetUnit = best->id; chase(u, *best); }
        } else if (automatic && u.targetUnit && u.attackPhase != AttackPhase::Windup && u.attackPhase != AttackPhase::Recovery &&
            (clock_.elapsedTicks() + u.id) % 6 == 0) {
            const auto* current = unit(u.targetUnit);
            if (current && !attackReach(u, *current)) {
                const auto* best = acquire(u.blockedTicks < 18);
                if (best && best->id != u.targetUnit) {
                    u.targetUnit = best->id; u.route.clear(); u.next = 0; u.blockedTicks = 0;
                    chase(u, *best);
                }
            }
        }
        if (!u.targetUnit && !ground) continue;
        const auto* target = unit(u.targetUnit);
        if (!ground && (!target || !canAttack(u, *target) || !targetVisible(u, *target) ||
            (u.currentOrder.kind == OrderKind::Hold && !attackReach(u, *target)))) {
            resumeOrder(u);
            continue;
        }
        const bool inReach = (ground ? groundAttackReach(u, u.currentOrder.cell) : attackReach(u, *target));
        const auto release = [&] { if (ground) releaseGroundAttack(u); else releaseAttack(u, *target, hits); };
        if (u.attackPhase == AttackPhase::Windup) {
            if (--u.attackTicks <= 0) {
                if (inReach) release();
                combat::recover(u);
            }
            continue;
        }
        if (u.attackPhase == AttackPhase::Recovery) continue;
        if (inReach) {
            u.route.clear(); u.next = 0; u.state = UnitState::Attacking;
            const auto delta = (ground ? center(u.currentOrder.cell) : target->position) - u.position;
            u.facing = {delta.x > .2f ? 1 : delta.x < -.2f ? -1 : 0, delta.y > .2f ? 1 : delta.y < -.2f ? -1 : 0};
            if (u.attackPhase == AttackPhase::Ready) {
                u.attackPhase = AttackPhase::Windup; u.attackTicks = u.definition.attackWindupTicks;
                if (u.attackTicks == 0) { release(); combat::recover(u); }
            }
        } else if (u.chaseTicks == 0) {
            if (ground) chaseGround(u); else chase(u, *target);
        }
    }
    resolveHits(hits);
}
}
