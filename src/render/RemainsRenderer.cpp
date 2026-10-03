#include "RenderSupport.hpp"
#include "rts/UnitAnimation.hpp"

namespace rts {
using namespace render;
namespace {
float ease(float progress) {
    const float t = std::clamp(progress, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
template<class Draw>
void sink(ID2D1RenderTarget* target, float& opacity, UiRect bounds, float progress, float zoom, Draw draw) {
    if (progress <= 0) { draw(Vec2{}); return; }
    const Vec2 offset{0, bounds.height * ease(progress)};
    const float visible = bounds.height - offset.y;
    if (visible <= 0) return;
    // Keep the soil edge fixed while lowering the sprite. Cropped pixels reveal
    // the already-painted terrain, including paint, slopes, fog and lighting.
    const float bottom = bounds.y + bounds.height;
    const float feather = std::min(3 * zoom, visible);
    const auto strip = [&](float top, float height) {
        if (height <= 0) return;
        target->PushAxisAlignedClip(rect(bounds.x - 1, top, bounds.width + 2, height), D2D1_ANTIALIAS_MODE_ALIASED);
        draw(offset);
        target->PopAxisAlignedClip();
    };
    strip(bounds.y + offset.y - 1, visible - feather + 1);
    const float previous = opacity;
    constexpr int strips = 6;
    for (int i = 0; i < strips; ++i) {
        opacity = previous * (1 - ease((i + .5f) / strips));
        strip(bottom - feather + feather * i / strips, feather / strips);
    }
    opacity = previous;
}
}
void Renderer::corpseSprite(const Corpse& corpse, const WorldView& view, unsigned color) {
    const auto ground = unitScreenAnchor(view, corpse.position, corpse.height);
    const auto& d = corpse.sprite;
    const float opacity = worldOpacity_;
    if (corpse.phase == CorpsePhase::Vanishing)
        worldOpacity_ *= 1 - ease(1 - float(corpse.remainingTicks) / corpse.vanishDuration);
    const float progress = corpse.phase == CorpsePhase::Sinking ? 1 - float(corpse.remainingTicks) / Corpse::sinkTicks : 0;
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
        sink(target_.Get(), worldOpacity_, {start.x, start.y, extent.x, extent.y}, progress, view.zoom, [&](Vec2 offset) {
            sprite(bitmap.Get(), rect(float(r[0]), float(r[1]), float(r[2]), float(r[3])), start + offset, extent, d.pixelArt, .5f);
        });
    } else if (corpse.phase == CorpsePhase::Vanishing) {
        // No persistent body or VFX for non-decomposing units, including the hero.
        unitImage(d, d.idle, corpse.facingRow, ground, view.zoom, color);
    } else {
        // Temporary fallen art for units without an authored death animation.
        const auto start = ground + Vec2{-32, -32} * view.zoom;
        sink(target_.Get(), worldOpacity_, {start.x, start.y, 64 * view.zoom, 64 * view.zoom}, progress, view.zoom, [&](Vec2 offset) {
            D2D1_MATRIX_3X2_F transform;
            target_->GetTransform(&transform);
            target_->SetTransform(D2D1::Matrix3x2F::Rotation(90, point(ground + offset)) * transform);
            unitPortrait(d, start + offset, Vec2{64, 64} * view.zoom, color);
            target_->SetTransform(transform);
        });
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
    if (!bones.sinking) worldOpacity_ *= ease(float(Bones::lifetimeTicks - bones.remainingTicks) / 12);
    const float progress = bones.sinking ? 1 - float(bones.remainingTicks) / Bones::sinkTicks : 0;
    sink(target_.Get(), worldOpacity_, {start.x, start.y, extent.x, extent.y}, progress, view.zoom, [&](Vec2 offset) {
        sprite(bitmap.Get(), rect(float(r[0]), float(r[1]), float(r[2]), float(r[3])), start + offset, extent, definition.pixelArt, .5f);
    });
    worldOpacity_ = opacity;
}
}
