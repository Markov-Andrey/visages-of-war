#include "rts/Simulation.hpp"
#include <algorithm>

namespace rts {
void Simulation::arbitrateTraffic() {
    // A movement order owns its right of way until the oncoming group passes.
    // The older order wins; edges always point to smaller order IDs, so a chain
    // of encounters cannot form a reciprocal yielding cycle. Stop/Hold and
    // combatants are never recruited into a yielding group.
    const auto eligible = [](const Unit& u) {
        return u.motion().moveGroup && u.orders().state == UnitState::Moving && u.motion().next < u.motion().route.size() &&
            (u.orders().currentOrder.kind == OrderKind::Move || u.orders().currentOrder.kind == OrderKind::AttackMove);
    };
    for (auto& u : world_.circles()) {
        if (!u.motion().yieldingTo) continue;
        bool approaching = false;
        for (const auto& other : world_.circles()) if (other.motion().moveGroup == u.motion().yieldingTo && eligible(other) &&
            groundDot(other.position - u.motion().yieldOrigin, u.motion().yieldForward) < 3) { approaching = true; break; }
        if (!eligible(u) || !approaching) {
            u.motion().yieldingTo = 0; u.motion().progressTicks = u.motion().stagnantTicks = u.motion().blockedTicks = 0;
        }
    }
    for (const auto& u : world_.circles()) {
        if (!eligible(u) || u.motion().yieldingTo || u.motion().blockedTicks < 30) continue;
        for (const auto index : movementCandidates(u.position, u.position, 4)) {
            const auto& other = world_.circles()[index];
            if (!eligible(other) || other.motion().moveGroup <= u.motion().moveGroup || other.motion().yieldingTo ||
                airborne(other.definition.movement) != airborne(u.definition.movement) ||
                groundDot(u.motion().formationForward, other.motion().formationForward) > -.3f ||
                groundLengthSquared(u.position - other.position) > 16) continue;
            const Vec2 forward = u.motion().formationForward * (1 / groundLength(u.motion().formationForward));
            const auto plane = groundToPlane(forward);
            const Vec2 side = planeToGround({-plane.y, plane.x});
            // Open ground already has reciprocal avoidance. Arbitrate only where
            // terrain prevents a normal side pass, including ramps and corners.
            if (map().canTraverse(u.position, u.position + side * 2, u.definition.collisionRadius, u.definition.movement) &&
                map().canTraverse(u.position, u.position - side * 2, u.definition.collisionRadius, u.definition.movement)) continue;
            const auto loser = other.motion().moveGroup;
            const auto origin = other.position;
            for (auto& member : world_.circles()) if (member.motion().moveGroup == loser && eligible(member)) {
                member.motion().yieldingTo = u.motion().moveGroup; member.motion().yieldOrigin = origin; member.motion().yieldForward = forward;
            }
        }
    }
}
}
