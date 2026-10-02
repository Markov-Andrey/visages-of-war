#pragma once
#include "rts/Types.hpp"
#include <algorithm>

namespace rts {
// Presentation only: independent of simulation ticks and camera movement.
struct MenuParallax {
    Vec2 offset{};
    void advance(Vec2 mouse, Vec2 extent, float seconds) {
        Vec2 goal{};
        if (extent.x > 0 && extent.y > 0 && mouse.x >= 0 && mouse.y >= 0 && mouse.x < extent.x && mouse.y < extent.y)
            goal = {2 * mouse.x / extent.x - 1, 2 * mouse.y / extent.y - 1};
        const float blend = 1 - std::exp(-7 * std::max(0.0f, seconds));
        offset = offset + (goal - offset) * blend;
    }
};
struct MenuBackdropLayout {
    Vec2 backgroundOrigin, backgroundSize, foregroundOrigin, foregroundSize;
    MenuBackdropLayout(Vec2 extent, Vec2 background, Vec2 foreground, Vec2 offset) {
        offset = {std::clamp(offset.x, -1.0f, 1.0f), std::clamp(offset.y, -1.0f, 1.0f)};
        // Shared artwork coordinates preserve composition on resize. Overscan
        // covers the full travel plus a filtering margin at either screen edge.
        const Vec2 travel{background.x * .005f, background.y * .006f};
        const float cover = std::max(extent.x / (background.x - 2 * (travel.x + 1)),
            extent.y / (background.y - 2 * (travel.y + 1)));
        backgroundSize = background * cover;
        const auto origin = (extent - backgroundSize) * .5f;
        const Vec2 shift{-offset.x * travel.x * cover, -offset.y * travel.y * cover};
        backgroundOrigin = origin + shift;
        foregroundOrigin = origin + Vec2{background.x * .143f, background.y * .05f} * cover + shift * 3;
        foregroundSize = foreground * (background.x * .43f / foreground.x * cover);
    }
};
}
