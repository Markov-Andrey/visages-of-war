#include "CombatRules.hpp"
#include "rts/UnitAnimation.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

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
    if (const auto& sockets = u.definition.sprite.projectileOrigins) {
        const auto& sprite = u.definition.sprite;
        const auto socket = (*sockets)[locomotionFrame(u).row];
        const Vec2 offset{(socket.x - sprite.anchor.x) * sprite.size.x, (socket.y - sprite.anchor.y) * sprite.size.y};
        // Split the authored screen-plane socket into lateral ground displacement and elevation.
        // UnitPresentation applies the common foot offset to both unit and projectile.
        p.start = p.position = p.previousPosition = u.position + Vec2{offset.x, -offset.x} * (1 / (2 * WorldView::tileSize));
        p.startHeight = p.height = p.previousHeight = unitHeight(u) - offset.y / WorldView::levelHeight;
    }
    p.aimHeight = aimHeight + p.definition.impactHeight;
    const Vec2 distance = p.aim - p.start;
    if (p.definition.launchAngle) {
        const float length = groundLength(distance) * WorldView::tileSize / groundPlaneScale;
        const float rise = p.aimHeight - p.startHeight;
        const float slope = std::tan(*p.definition.launchAngle * std::numbers::pi_v<float> / 180);
        // y(t) = lerp(start,end,t) + 4*h*t*(1-t). Steep uphill shots use the direct line.
        p.definition.arcHeight = std::max(0.0f, (length * slope / WorldView::levelHeight - rise) * .25f);
    }
    p.flightTicks = std::max(1, static_cast<int>(std::ceil(groundLength(distance) / p.definition.speed * ticksPerSecond)));
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
                if (groundLengthSquared(delta) <= p.definition.splashRadius * p.definition.splashRadius)
                    hits.push_back({target.id, p.damage, p.owner});
            }
        }
        projectileImpacts_.push_back({p.position, p.aimHeight, p.definition.splashRadius});
    }
    std::erase_if(projectiles_, [](const auto& p) { return p.elapsedTicks >= p.flightTicks; });
}
}
