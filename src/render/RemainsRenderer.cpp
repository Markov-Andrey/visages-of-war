#include "RenderSupport.hpp"
#include "rts/UnitAnimation.hpp"

namespace rts {
using namespace render;
namespace {
float ease(float progress) {
    const float t = std::clamp(progress, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
}
void Renderer::corpseSprite(const Corpse& corpse, const WorldView& view, unsigned color) {
    const auto ground = unitScreenAnchor(view, corpse.position, corpse.height);
    const auto& d = corpse.sprite;
    const float opacity = worldOpacity_;
    if (corpse.phase == CorpsePhase::Vanishing)
        worldOpacity_ *= 1 - ease(1 - float(corpse.remainingTicks) / corpse.vanishDuration);
    else if (corpse.phase == CorpsePhase::Fading)
        worldOpacity_ *= ease(float(corpse.remainingTicks) / Corpse::fadeTicks);
    if (const auto* frame = corpseFrame(corpse)) {
        const auto& death = *d.death;
        const auto path = imagePath(death.image);
        if (d.teamMask == SpriteTeamMask::None) color = 0;
        auto& bitmap = unitSheets_[{path, color, d.teamMask}];
        if (!bitmap) loadBitmap(paths_.asset(path), bitmap, color, d.teamMask, {}, true);
        const auto& r = frame->source;
        const float scale = death.scale * view.zoom;
        const auto start = ground - frame->anchor * scale;
        const Vec2 extent = Vec2{float(r[2]), float(r[3])} * scale;
        sprite(bitmap.Get(), rect(float(r[0]), float(r[1]), float(r[2]), float(r[3])), start, extent, d.pixelArt, .5f);
    } else if (corpse.phase == CorpsePhase::Vanishing) {
        // No persistent body or VFX for non-decomposing units, including the hero.
        unitImage(d, d.idle, corpse.facingRow, ground, view.zoom, color);
    } else {
        // Temporary fallen art for units without an authored death animation.
        const auto start = ground + Vec2{-32, -32} * view.zoom;
        D2D1_MATRIX_3X2_F transform;
        target_->GetTransform(&transform);
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(90, point(ground)) * transform);
        unitPortrait(d, start, Vec2{64, 64} * view.zoom, color);
        target_->SetTransform(transform);
    }
    worldOpacity_ = opacity;
}
void Renderer::bonesSprite(const Bones& bones, const WorldView& view) {
    const auto& definition = worldAssets_.bonesSprite();
    const auto& frame = definition.variants.at(bones.variation % definition.variants.size());
    auto& bitmap = worldSprites_[definition.image];
    if (!bitmap) loadBitmap(paths_.asset(definition.image), bitmap, 0, SpriteTeamMask::None, {}, true);
    const float scale = definition.scale * view.zoom;
    const auto ground = unitScreenAnchor(view, bones.position, bones.height);
    const auto start = ground - frame.anchor * scale;
    const auto& r = frame.source;
    const Vec2 extent = Vec2{float(r[2]), float(r[3])} * scale;
    const float opacity = worldOpacity_;
    worldOpacity_ *= bones.fading ? ease(float(bones.remainingTicks) / Bones::fadeTicks)
                                 : ease(float(Bones::lifetimeTicks - bones.remainingTicks) / 12);
    sprite(bitmap.Get(), rect(float(r[0]), float(r[1]), float(r[2]), float(r[3])), start, extent, definition.pixelArt, .5f);
    worldOpacity_ = opacity;
}
}
