#include "CombatRules.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace rts {
Simulation::CombatTarget Simulation::combatTarget(EntityId id) const {
    if (const auto* target = unit(id)) return {target};
    return {nullptr, building(id)};
}
bool Simulation::targetVisible(const Unit& observer, CombatTarget target) const {
    if (!target) return false;
    if (target.unit) return visibleTo(observer.owner, target.unit->cell);
    const auto& b = *target.building;
    for (int y = 0; y < b.definition.height; ++y) for (int x = 0; x < b.definition.width; ++x)
        if (visibleTo(observer.owner, b.origin + Cell{x, y})) return true;
    return false;
}
bool Simulation::canAttack(const Unit& attacker, const Unit& target) const {
    return canAttack(attacker, target.definition.collision == CollisionShape::Footprint ? CombatTarget{nullptr, &target} : CombatTarget{&target}, false);
}
bool Simulation::canAttack(const Unit& attacker, EntityId target, bool forced) const {
    return canAttack(attacker, combatTarget(target), forced);
}
bool Simulation::canAttack(const Unit& attacker, CombatTarget target, bool forced) const {
    return target && attacker.complete() && attacker.health > 0 && attacker.id != target.id() && attacker.attackDamage() > 0 && target.health() > 0 &&
        (forced || combat::hostile(attacker.owner, target.owner())) &&
        combat::accepts(attacker.definition.attackTargets, airborne(attacker.definition.movement), target.air());
}
Vec2 Simulation::attackAim(CombatTarget target, Vec2 from) const {
    if (target.unit) return target.unit->position;
    const auto& b = *target.building;
    const float x = float(b.origin.x), y = float(b.origin.y), right = x + b.definition.width, bottom = y + b.definition.height;
    if (from.x >= x && from.x <= right && from.y >= y && from.y <= bottom) return from;
    const std::array<Vec2, 4> corners{{{x,y}, {right,y}, {right,bottom}, {x,bottom}}};
    Vec2 best = corners.front();
    for (size_t i = 0; i < corners.size(); ++i) {
        const auto a = corners[i], edge = corners[(i + 1) % corners.size()] - a;
        const auto point = a + edge * std::clamp(groundDot(from - a, edge) / groundLengthSquared(edge), 0.f, 1.f);
        if (groundLengthSquared(point - from) < groundLengthSquared(best - from)) best = point;
    }
    return best;
}
bool Simulation::attackReach(const Unit& attacker, CombatTarget target, std::optional<Vec2> origin) const {
    if (!canAttack(attacker, target, true)) return false;
    Vec2 start = origin.value_or(attacker.position);
    Vec2 aim = attackAim(target, start);
    if (attacker.definition.collision == CollisionShape::Footprint) {
        start = attackAim(combatTarget(attacker.id), aim);
        const auto outward = aim - start; const auto distance = groundLength(outward);
        if (distance > .005f) start = start + outward * (.005f / distance);
        aim = attackAim(target, start);
    }
    const Vec2 delta = aim - start;
    if (groundLengthSquared(delta) > attacker.definition.attackRange * attacker.definition.attackRange) return false;
    if (attacker.definition.projectile) return true;
    if (target.building) {
        // Trace only to the outside of the square footprint: the target itself is solid.
        const float distance = groundLength(delta);
        if (distance < .005f) return airborne(attacker.definition.movement);
        const Vec2 outside = aim - delta * (.005f / distance);
        if (!map().contains(cellAt(outside))) return false;
        if (!airborne(attacker.definition.movement) &&
            std::abs(map().surfaceHeight(cellAt(outside), outside) - map().at(target.building->origin).height) > .02f) return false;
        return map().canTraverse(start, outside, .001f, attacker.definition.movement);
    }
    const Cell from = cellAt(start), d = target.unit->cell - from;
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
bool Simulation::attack(std::span<const EntityId> ids, EntityId targetId, bool forced) {
    const auto target = combatTarget(targetId);
    if (!target) return false;
    bool any = false;
    for (EntityId id : ids) {
        auto* u = mutableEntity(id);
        if (!u || u->owner != player_.id || !canAttack(*u, target, forced) || !targetVisible(*u, target)) continue;
        Order order{OrderKind::Attack, {}, targetId}; order.forceAttack = forced;
        issue(*u, order); any = true;
    }
    setMessage(any ? L"Атакуем выбранную цель." : L"Выбранные юниты не могут атаковать эту цель.");
    return any;
}
void Simulation::chase(Unit& u, CombatTarget target) {
    if (attackReach(u, target)) {
        u.route.clear(); u.next = 0; u.blockedTicks = 0; u.state = UnitState::Attacking;
        return;
    }
    approachCombat(u, attackAim(target, u.position), &target);
}
}
