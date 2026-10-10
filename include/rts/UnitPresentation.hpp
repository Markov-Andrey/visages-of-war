#pragma once
#include "rts/Projectile.hpp"
#include <algorithm>

namespace rts {
// Feet sit in the lower third of the projected diamond.
// This is presentation only. Positions, occupancy, vision and minimap markers
// continue to use logical map coordinates and the original terrain height.
inline constexpr float unitGroundOffset = 1.0f / 3;
inline Vec2 unitScreenOffset(const WorldView& view) {
    return {0, WorldView::tileSize * view.zoom * unitGroundOffset};
}
inline Vec2 unitScreenAnchor(const WorldView& view, Vec2 position, float height = 0) {
    return view.project(position, height) + unitScreenOffset(view);
}
inline float unitDrawDepth(Vec2 position) { return position.x + position.y + unitGroundOffset * 2; }
inline Vec2 projectileScreenPosition(const WorldView& view, const Projectile& projectile, bool previous = false) {
    const int elapsed = previous ? std::max(0, projectile.elapsedTicks - 1) : projectile.elapsedTicks;
    const float progress = std::clamp(float(elapsed) / std::max(1, projectile.flightTicks), 0.0f, 1.0f);
    // Both ends of a homing shot follow unit art. Ground shots start at the
    // shifted shooter but land at the actual ordered point/splash centre.
    const float shift = projectile.definition.targeting == ProjectileTargeting::Unit ? 1 : 1 - progress;
    return view.project(previous ? projectile.previousPosition : projectile.position,
        previous ? projectile.previousHeight : projectile.height) + unitScreenOffset(view) * shift;
}
inline Vec2 projectileScreenTangent(const WorldView& view, const Projectile& projectile) {
    const float t = std::clamp(float(projectile.elapsedTicks) / std::max(1, projectile.flightTicks), 0.0f, 1.0f);
    const float rise = projectile.aimHeight - projectile.startHeight + 4 * projectile.definition.arcHeight * (1 - 2 * t);
    const auto tangent = view.project(projectile.aim - projectile.start, rise) - view.origin;
    return tangent - unitScreenOffset(view) * (projectile.definition.targeting == ProjectileTargeting::Point ? 1.0f : 0.0f);
}
}
