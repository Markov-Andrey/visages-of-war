#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::drawCrystal(const Crystal& crystal, Vec2 ground, float zoom, bool glowing, std::uint64_t tick) {
    const auto& art = worldAssets_.crystalSprite();
    // Position is authored and shared by Forge and the match; match-assigned IDs
    // must not reshuffle the variants when entering play or depleting a neighbour.
    auto seed = std::uint32_t(crystal.cell.x) * 0x9e3779b9u ^ std::uint32_t(crystal.cell.y) * 0x85ebca6bu;
    seed ^= seed >> 16;
    const auto& frame = art.variants.at(seed % art.variants.size());
    const auto& r = frame.source;
    const float scale = art.scale * zoom;
    auto* emission = glowing ? spriteLights_.at(crystal_.Get()).highlights.Get() : nullptr;
    if (emission) drawLightGlow(ground + Vec2{0, -22} * zoom, 36 * zoom, art.glowColor, crystalPulse(crystal.id, tick));
    sprite(crystal_.Get(), rect(float(r[0]), float(r[1]), float(r[2]), float(r[3])),
        ground - frame.anchor * scale, Vec2{float(r[2]), float(r[3])} * scale, false, 1, emission);
}

void Renderer::drawResourceIcon(const std::string& id, UiRect bounds) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const auto& path = worldAssets_.resourceIcons().at(id);
    auto& bitmap = unitUiImages_[path];
    if (!bitmap) loadBitmap(paths_.asset(path), bitmap);
    const auto pixels = bitmap->GetSize();
    const float scale = std::min(bounds.width / pixels.width, bounds.height / pixels.height);
    const Vec2 extent{pixels.width * scale, pixels.height * scale};
    sprite(bitmap.Get(), rect(0, 0, pixels.width, pixels.height),
        {bounds.x + (bounds.width - extent.x) * .5f, bounds.y + (bounds.height - extent.y) * .5f}, extent, false, 0);
}
}
