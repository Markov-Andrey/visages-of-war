#pragma once
#include "rts/Simulation.hpp"

namespace rts::combat {
inline bool hostile(PlayerId a, PlayerId b) { return a != b && a != neutralPlayer && b != neutralPlayer; }
inline bool accepts(AttackTargets targets, bool sourceAir, bool targetAir) {
    return targets == AttackTargets::All || (targets == AttackTargets::SameLayer && sourceAir == targetAir) ||
        (targets == AttackTargets::Ground && !targetAir) || (targets == AttackTargets::Air && targetAir);
}
inline void recover(Unit& u) {
    if (u.definition.attackRecoveryTicks == 0 && u.definition.attackCooldownTicks == 0) {
        // The next preparation starts at this boundary, without a hidden extra tick.
        u.combat().attackPhase = AttackPhase::Windup; u.combat().attackTicks = u.definition.attackWindupTicks;
        return;
    }
    u.combat().attackPhase = u.definition.attackRecoveryTicks > 0 ? AttackPhase::Recovery : AttackPhase::Cooldown;
    u.combat().attackTicks = u.definition.attackRecoveryTicks > 0 ? u.definition.attackRecoveryTicks : u.definition.attackCooldownTicks;
}
}
