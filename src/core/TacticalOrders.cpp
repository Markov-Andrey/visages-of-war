#include "rts/Simulation.hpp"
#include <algorithm>
#include <cmath>

namespace rts {
void Simulation::resumeOrder(Unit& u) {
    u.targetUnit = 0;
    cancelAttack(u);
    if (u.pendingOrder) return;
    if (u.progress > 0) {
        // Finish the reserved step before resuming the original destination.
        u.route.resize(u.next + 1);
        return;
    }
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
    return delta.x * delta.x + delta.y * delta.y <= u.definition.attackRange * u.definition.attackRange;
}
void Simulation::chaseGround(Unit& u) {
    if (u.progress > 0) return;
    u.chaseTicks = 15;
    const auto target = u.currentOrder.cell;
    if (groundAttackReach(u, target)) {
        u.route.clear(); u.next = 0; u.blockedTicks = 0; u.state = UnitState::Attacking;
        return;
    }
    std::vector<Cell> slots;
    const auto held = occupied(u, true);
    const int radius = static_cast<int>(std::ceil(u.definition.attackRange));
    for (int y = std::max(0, target.y - radius); y <= std::min(map().height() - 1, target.y + radius); ++y)
        for (int x = std::max(0, target.x - radius); x <= std::min(map().width() - 1, target.x + radius); ++x) {
            const Cell c{x, y};
            if (map().walkable(c, u.definition.movement) && groundAttackReach(u, target, c) &&
                std::find(held.begin(), held.end(), c) == held.end()) slots.push_back(c);
        }
    if (!setRoute(u, slots, UnitState::ToAttack)) u.state = UnitState::ToAttack;
}
}
