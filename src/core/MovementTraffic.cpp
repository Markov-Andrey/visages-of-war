#include "rts/Simulation.hpp"
#include <algorithm>

namespace rts {
void Simulation::arbitrateTraffic() {
    // A movement order owns its right of way until the oncoming group passes.
    // The older order wins; edges always point to smaller order IDs, so a chain
    // of encounters cannot form a reciprocal yielding cycle. Stop/Hold and
    // combatants are never recruited into a yielding group.
    const auto eligible = [](const Unit& u) {
        return u.moveGroup && u.state == UnitState::Moving && u.next < u.route.size() &&
            (u.currentOrder.kind == OrderKind::Move || u.currentOrder.kind == OrderKind::AttackMove);
    };
    for (auto& u : units_) {
        if (!u.yieldingTo) continue;
        bool approaching = false;
        for (const auto& other : units_) if (other.moveGroup == u.yieldingTo && eligible(other) &&
            groundDot(other.position - u.yieldOrigin, u.yieldForward) < 3) { approaching = true; break; }
        if (!eligible(u) || !approaching) {
            u.yieldingTo = 0; u.progressTicks = u.stagnantTicks = u.blockedTicks = 0;
        }
    }
    for (const auto& u : units_) {
        if (!eligible(u) || u.yieldingTo || u.blockedTicks < 30) continue;
        for (const auto index : movementCandidates(u.position, u.position, 4)) {
            const auto& other = units_[index];
            if (!eligible(other) || other.moveGroup <= u.moveGroup || other.yieldingTo ||
                airborne(other.definition.movement) != airborne(u.definition.movement) ||
                groundDot(u.formationForward, other.formationForward) > -.3f ||
                groundLengthSquared(u.position - other.position) > 16) continue;
            const Vec2 forward = u.formationForward * (1 / groundLength(u.formationForward));
            const auto plane = groundToPlane(forward);
            const Vec2 side = planeToGround({-plane.y, plane.x});
            // Open ground already has reciprocal avoidance. Arbitrate only where
            // terrain prevents a normal side pass, including ramps and corners.
            if (map().canTraverse(u.position, u.position + side * 2, u.definition.collisionRadius, u.definition.movement) &&
                map().canTraverse(u.position, u.position - side * 2, u.definition.collisionRadius, u.definition.movement)) continue;
            const auto loser = other.moveGroup;
            const auto origin = other.position;
            for (auto& member : units_) if (member.moveGroup == loser && eligible(member)) {
                member.yieldingTo = u.moveGroup; member.yieldOrigin = origin; member.yieldForward = forward;
            }
        }
    }
}
}
