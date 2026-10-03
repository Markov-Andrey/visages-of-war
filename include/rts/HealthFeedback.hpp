#pragma once
#include <algorithm>
#include <cstdint>

namespace rts {
// Presentation-only damage history. It never delays or changes actual health.
// Timing uses the 30 Hz simulation clock, so pause and off-screen time agree.
class HealthFeedback {
public:
    static constexpr int holdTicks = 12;  // 400 ms to notice the hit.
    static constexpr int drainTicks = 18; // 600 ms of smooth right-to-left decay.
    float remaining(std::uint64_t now) const {
        if (damage_ <= 0 || now < lastHit_) return 0;
        const auto age = now - lastHit_;
        if (age <= holdTicks) return damage_;
        if (age >= holdTicks + drainTicks) return 0;
        const float t = float(age - holdTicks) / drainTicks;
        return damage_ * (1 - t * t * (3 - 2 * t));
    }
    void record(int before, int after, std::uint64_t tick) {
        if (after <= 0) { damage_ = 0; lastHit_ = 0; return; }
        if (after >= before) return;
        damage_ = remaining(tick) + float(before - after);
        lastHit_ = tick;
    }
private:
    float damage_{};
    std::uint64_t lastHit_{};
};
}
