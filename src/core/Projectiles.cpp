#include "CombatRules.hpp"
#include <algorithm>
#include <cmath>

namespace rts {
void Simulation::releaseAttack(Unit& u, const Unit& target, std::vector<Hit>& hits) {
    if (!u.definition.projectile) { hits.push_back({target.id, u.attackDamage(), u.owner}); return; }
    const bool homing = u.definition.projectile->targeting == ProjectileTargeting::Unit;
    const Cell cell{int(std::floor(target.position.x)), int(std::floor(target.position.y))};
    releaseProjectile(u, target.position, homing ? unitHeight(target) : map().surfaceHeight(cell, target.position), homing ? target.id : 0);
}
void Simulation::releaseGroundAttack(Unit& u) {
    const auto aim = center(u.currentOrder.cell);
    releaseProjectile(u, aim, map().surfaceHeight(u.currentOrder.cell, aim), 0);
}
void Simulation::releaseProjectile(Unit& u, Vec2 aim, float aimHeight, EntityId target) {
    Projectile p;
    p.definition = *u.definition.projectile;
    p.targets = u.definition.attackTargets; p.owner = u.owner; p.sourceAir = airborne(u.definition.movement);
    p.target = target;
    p.damage = u.attackDamage(); p.start = p.position = p.previousPosition = u.position; p.aim = aim;
    p.startHeight = p.height = p.previousHeight = unitHeight(u) + p.definition.launchHeight;
    p.aimHeight = aimHeight + p.definition.impactHeight;
    const Vec2 distance = p.aim - p.start;
    p.flightTicks = std::max(1, static_cast<int>(std::ceil(std::hypot(distance.x, distance.y) / p.definition.speed * ticksPerSecond)));
    projectiles_.push_back(std::move(p));
}
void Simulation::tickProjectiles(std::vector<Hit>& hits) {
    for (auto& impact : projectileImpacts_) --impact.remainingTicks;
    std::erase_if(projectileImpacts_, [](const auto& impact) { return impact.remainingTicks <= 0; });
    for (auto& p : projectiles_) {
        p.previousPosition = p.position; p.previousHeight = p.height;
        if (p.target) if (const auto* target = unit(p.target); target && target->health > 0) {
            p.aim = target->position; p.aimHeight = unitHeight(*target) + p.definition.impactHeight;
        }
        const float t = std::min(1.0f, float(++p.elapsedTicks) / p.flightTicks);
        p.position = p.start + (p.aim - p.start) * t;
        p.height = std::lerp(p.startHeight, p.aimHeight, t) + 4 * p.definition.arcHeight * t * (1 - t);
        if (p.elapsedTicks < p.flightTicks) continue;
        if (p.definition.targeting == ProjectileTargeting::Unit) {
            if (const auto* target = unit(p.target); target && target->health > 0 && combat::hostile(p.owner, target->owner) &&
                combat::accepts(p.targets, p.sourceAir, airborne(target->definition.movement)))
                hits.push_back({target->id, p.damage, p.owner});
        } else {
            for (const auto& target : units_) {
                if ((!p.definition.friendlyFire && !combat::hostile(p.owner, target.owner)) ||
                    !combat::accepts(p.targets, p.sourceAir, airborne(target.definition.movement))) continue;
                const Vec2 delta = target.position - p.aim;
                if (delta.x * delta.x + delta.y * delta.y <= p.definition.splashRadius * p.definition.splashRadius)
                    hits.push_back({target.id, p.damage, p.owner});
            }
        }
        projectileImpacts_.push_back({p.position, p.aimHeight, p.definition.splashRadius});
    }
    std::erase_if(projectiles_, [](const auto& p) { return p.elapsedTicks >= p.flightTicks; });
}
}
