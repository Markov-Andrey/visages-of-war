#pragma once
#include "rts/Menu.hpp"

namespace rts {
inline constexpr std::array<const wchar_t*, 7> loadingImagePaths{
    L"ui/loading/sky.png", L"ui/loading/celestials.png", L"ui/loading/mountains.png",
    L"ui/loading/citadels.png", L"ui/loading/foreground.png",
    L"ui/loading/minimap-frame.png", L"ui/loading/progress-atlas.png"};

// Shared authoring plane keeps the cutout layers registered on every aspect ratio.
struct LoadingBackdropLayout {
    Vec2 origin, size, offset;
    float scale;
    LoadingBackdropLayout(Vec2 extent, Vec2 pointer) {
        offset = {std::clamp(pointer.x, -1.f, 1.f), std::clamp(pointer.y, -1.f, 1.f)};
        scale = std::max(extent.x / (1672 - 40.f), extent.y / (941 - 30.f));
        size = Vec2{1672, 941} * scale;
        origin = (extent - size) * .5f;
    }
    UiRect layer(UiRect authored, float depth) const {
        return {origin.x + (authored.x - offset.x * 14 * depth) * scale,
            origin.y + (authored.y - offset.y * 9 * depth) * scale,
            authored.width * scale, authored.height * scale};
    }
};

struct LoadingLayout {
    UiRect map, frame, title, bar, fill, caption, ornament, status;
    explicit LoadingLayout(Vec2 extent) {
        const float scale = std::min({1.4f, extent.x / 1100, extent.y / 900});
        const float side = 360 * scale, center = extent.x * .5f;
        map = {center - side * .5f, extent.y * .43f - side * .5f, side, side};
        // Inner opening of the 1254px frame; top/bottom diamonds overhang the map.
        const float artScale = side / 880;
        frame = {map.x - 187 * artScale, map.y - 187 * artScale, 1254 * artScale, 1254 * artScale};
        title = {center - 350 * scale, map.y - 106 * scale, 700 * scale, 48 * scale};
        bar = {center - 340 * scale, map.y + side + 67 * scale, 680 * scale, 248 * (680 * scale / 2172)};
        const float barScale = bar.width / 2172;
        fill = {bar.x + 365 * barScale, bar.y + 73 * barScale, 1435 * barScale, 70 * barScale};
        caption = {center - 145 * scale, bar.y + bar.height + 8 * scale, 290 * scale, 44 * scale};
        ornament = {center - 255 * scale, caption.y + caption.height + 3 * scale, 510 * scale, 66 * scale};
        status = {center - 350 * scale, ornament.y + ornament.height + 7 * scale, 700 * scale, 28 * scale};
    }
};
}
