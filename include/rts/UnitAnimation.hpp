#pragma once
#include "rts/Simulation.hpp"
#include <array>

namespace rts {
struct SpriteFrame { int column, row; };
inline const UnitDeathFrame* corpseFrame(const Corpse& corpse) {
    if (!corpse.sprite.death || corpse.sprite.death->frames.empty()) return nullptr;
    const auto& death = *corpse.sprite.death;
    const auto index = static_cast<size_t>(std::max(0, corpse.ageTicks()) / std::max(1, death.ticksPerFrame));
    return &death.frames[std::min(index, death.frames.size() - 1)];
}
inline SpriteFrame locomotionFrame(const Unit& unit) {
    // Animation columns and directional rows come from each entity sprite definition.
    // Rows run clockwise on screen: S, SE, E, NE, N, NW, W, SW.
    constexpr std::array<Cell, 8> facings{{{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}}};
    int row = 0;
    for (size_t i = 0; i < facings.size(); ++i) if (unit.facing == facings[i]) row = static_cast<int>(i);
    const bool walking = unit.next < unit.route.size() && unit.position != unit.tickPosition;
    const auto& sprite = unit.definition.sprite;
    return {walking ? sprite.walk[static_cast<size_t>(unit.walkCycle * sprite.walk.size()) % sprite.walk.size()] : sprite.idle, sprite.rows[row]};
}
inline SpriteFrame unitFrame(const Unit& unit) {
    auto frame = locomotionFrame(unit);
    const auto& d = unit.definition;
    const bool windup = unit.attackPhase == AttackPhase::Windup;
    if (windup || unit.attackPhase == AttackPhase::Recovery) {
        const auto& frames = windup ? d.sprite.windup : d.sprite.recovery;
        const int duration = windup ? d.attackWindupTicks : d.attackRecoveryTicks;
        const auto index = static_cast<size_t>(std::max(0, duration - unit.attackTicks)) * frames.size() / std::max(1, duration);
        frame.column = frames[std::min(index, frames.size() - 1)];
    }
    return frame;
}
}
