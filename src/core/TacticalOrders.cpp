#include "rts/Simulation.hpp"
#include <algorithm>
#include <cmath>

namespace rts {
void Simulation::resumeOrder(Unit& u) {
    u.combat().attackTarget = 0;
    cancelAttack(u);
    if (u.orders().currentOrder.kind == OrderKind::AttackMove || u.orders().currentOrder.kind == OrderKind::Patrol) {
        const Cell goal = u.orders().currentOrder.cell;
        setRoute(u, std::span<const Cell>(&goal, 1), UnitState::Moving);
    } else {
        u.motion().route.clear(); u.motion().next = 0; u.orders().state = UnitState::Idle;
        if (u.orders().currentOrder.kind != OrderKind::Hold) u.orders().currentOrder = {OrderKind::Stop};
    }
}
bool Simulation::groundAttackReach(const Unit& u, Cell target, std::optional<Cell> from) const {
    const Vec2 start = from ? center(*from) : u.definition.collision == CollisionShape::Footprint ? attackAim(combatTarget(u.id), center(target)) : u.position;
    const Vec2 delta = center(target) - start;
    return groundLengthSquared(delta) <= u.definition.attackRange * u.definition.attackRange;
}
void Simulation::chaseGround(Unit& u) {
    const auto target = u.orders().currentOrder.cell;
    if (groundAttackReach(u, target)) {
        u.motion().route.clear(); u.motion().next = 0; u.motion().blockedTicks = 0; u.orders().state = UnitState::Attacking;
        return;
    }
    approachCombat(u, center(target), nullptr);
}
}
