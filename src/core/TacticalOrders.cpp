#include "rts/Simulation.hpp"
#include <algorithm>
#include <cmath>

namespace rts {
void Simulation::resumeOrder(Unit& u) {
    u.targetUnit = 0;
    cancelAttack(u);
    if (u.currentOrder.kind == OrderKind::AttackMove || u.currentOrder.kind == OrderKind::Patrol) {
        const Cell goal = u.currentOrder.cell;
        setRoute(u, std::span<const Cell>(&goal, 1), UnitState::Moving);
    } else {
        u.route.clear(); u.next = 0; u.state = UnitState::Idle;
        if (u.currentOrder.kind != OrderKind::Hold) u.currentOrder = {OrderKind::Stop};
    }
}
bool Simulation::groundAttackReach(const Unit& u, Cell target, std::optional<Cell> from) const {
    const Vec2 delta = center(target) - (from ? center(*from) : u.position);
    return groundLengthSquared(delta) <= u.definition.attackRange * u.definition.attackRange;
}
void Simulation::chaseGround(Unit& u) {
    const auto target = u.currentOrder.cell;
    if (groundAttackReach(u, target)) {
        u.route.clear(); u.next = 0; u.blockedTicks = 0; u.state = UnitState::Attacking;
        return;
    }
    approachCombat(u, center(target), nullptr);
}
}
