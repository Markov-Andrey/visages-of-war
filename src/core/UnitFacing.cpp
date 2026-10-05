#include "rts/UnitFacing.hpp"
#include <algorithm>
#include <array>

namespace rts {
void MotionFacing::reset() { *this = {}; }
Cell MotionFacing::update(Cell current, Vec2 displacement, float nominalStep) {
    // Tiny collision corrections must not turn the sprite. Blocked ticks also
    // interrupt a pending turn, but do not make the next correction a fresh start.
    if (std::hypot(displacement.x, displacement.y) < std::max(.00001f, nominalStep * .15f)) {
        average_ = average_ * .65f;
        pendingTicks_ = 0;
        return current;
    }
    average_ = initialized_ ? average_ * .65f + displacement * .35f : displacement;
    constexpr std::array<Cell, 8> headings{{{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};
    const int heading = int(std::lround(std::atan2(average_.y, average_.x) / .7853981634f));
    const Cell candidate = headings[(heading + 8) % 8];
    if (!initialized_) {
        initialized_ = true;
        return candidate; // A new command responds on its first meaningful step.
    }
    const float alignment = average_.x * current.x + average_.y * current.y;
    const float magnitude = std::hypot(average_.x, average_.y) * std::hypot(float(current.x), float(current.y));
    // Retain the current 45-degree sprite sector for an extra 10 degrees on
    // either side. This prevents flicker when travelling along a sector boundary.
    if (candidate == current || alignment >= magnitude * .84339145f) { // cos(32.5 degrees)
        pendingTicks_ = 0;
        return current;
    }
    if (candidate != pending_) { pending_ = candidate; pendingTicks_ = 0; }
    // Three consistent ticks (100 ms at 30 Hz) reject momentary avoidance turns.
    if (++pendingTicks_ < 3) return current;
    pendingTicks_ = 0;
    return candidate;
}
}
