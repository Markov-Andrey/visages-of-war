#pragma once
#include "rts/Match.hpp"
#include <algorithm>

namespace rts {
// One presentation player for every ability. Effects may sustain the same action
// while working; the activation clip always takes priority over sustained work.
class AbilityAnimationPlayer {
public:
    struct Clip {
        int abilityId{};
        AbilityAnimation action = AbilityAnimation::Stand;
        int elapsedTicks{}, durationTicks{};
    };
    void start(const AbilityDefinition& ability, const EntityDefinition& unit) {
        activation_ = makeClip(ability, unit);
    }
    void sustain(const AbilityDefinition& ability, const EntityDefinition& unit) {
        if (!ability.animation) return;
        if (!channel_ || channel_->abilityId != ability.id || channel_->action != *ability.animation)
            channel_ = makeClip(ability, unit);
        sustained_ = true;
    }
    void beginTick() {
        if (activation_ && ++activation_->elapsedTicks >= activation_->durationTicks) activation_.reset();
        if (channel_) channel_->elapsedTicks = (channel_->elapsedTicks + 1) % channel_->durationTicks;
        sustained_ = false;
    }
    void endTick() { if (!sustained_) channel_.reset(); }
    void clear() { activation_.reset(); channel_.reset(); sustained_ = false; }
    const Clip* current() const { return activation_ ? &*activation_ : channel_ ? &*channel_ : nullptr; }
private:
    static std::optional<Clip> makeClip(const AbilityDefinition& ability, const EntityDefinition& unit) {
        if (!ability.animation) return std::nullopt;
        const int duration = *ability.animation == AbilityAnimation::Attack ?
            std::max(1, unit.attackWindupTicks) + std::max(1, unit.attackRecoveryTicks) : simulationTicksPerSecond;
        return Clip{ability.id, *ability.animation, 0, duration};
    }
    std::optional<Clip> activation_, channel_;
    bool sustained_{};
};
inline int abilityAnimationColumn(const EntityDefinition& unit, AbilityAnimation action, int elapsedTicks) {
    const auto& sprite = unit.sprite;
    if (action == AbilityAnimation::Work) return sprite.work >= 0 ? sprite.work : sprite.idle;
    if (action != AbilityAnimation::Attack) return sprite.idle;
    // Missing attack artwork falls back for the entire action, not just one phase.
    if (sprite.windup.empty() || sprite.recovery.empty()) return sprite.idle;
    const int windup = std::max(1, unit.attackWindupTicks), recovery = std::max(1, unit.attackRecoveryTicks);
    const int tick = std::max(0, elapsedTicks) % (windup + recovery);
    const auto& frames = tick < windup ? sprite.windup : sprite.recovery;
    const int elapsed = tick < windup ? tick : tick - windup;
    return frames[static_cast<size_t>(elapsed) * frames.size() / (tick < windup ? windup : recovery)];
}
}
