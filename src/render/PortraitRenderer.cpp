#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::drawPortraitBackdrop(UiRect bounds, unsigned color) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const auto area = rect(bounds.x, bounds.y, bounds.width, bounds.height);
    brush_->SetColor(D2D1::ColorF(0x000000));
    target_->FillRectangle(area, brush_.Get());

    auto& gradient = portraitGradients_[color];
    if (!gradient) {
        // Keep the black team's center visible against the black backing.
        const unsigned tint = color ? color : 0x303038;
        const D2D1_GRADIENT_STOP stops[]{
            {0.0f, D2D1::ColorF(tint, .75f)},
            {0.5f, D2D1::ColorF(tint, .75f)},
            {0.9f, D2D1::ColorF(tint, 0.0f)},
            {1.0f, D2D1::ColorF(tint, 0.0f)}
        };
        ComPtr<ID2D1GradientStopCollection> collection;
        check(target_->CreateGradientStopCollection(stops, UINT(std::size(stops)), D2D1_GAMMA_2_2,
            D2D1_EXTEND_MODE_CLAMP, collection.GetAddressOf()));
        check(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(), D2D1::Point2F(), 1, 1), collection.Get(), gradient.GetAddressOf()));
    }
    gradient->SetCenter(D2D1::Point2F(bounds.x + bounds.width * .5f, bounds.y + bounds.height * .5f));
    gradient->SetRadiusX(bounds.width * .5f);
    gradient->SetRadiusY(bounds.height * .5f);
    target_->FillRectangle(area, gradient.Get());
}
void Renderer::portraitVitals(const SelectionPanelLayout& layout, int current, int maximum, unsigned color, int mana, int maximumMana) {
    centeredText(std::to_wstring(current) + L" / " + std::to_wstring(maximum), layout.health, color);
    if (maximumMana > 0)
        centeredText(std::to_wstring(mana) + L" / " + std::to_wstring(maximumMana), layout.mana, 0x8bbaff);
}

void Renderer::buildingPortrait(const BuildingSpriteStage& stage, UiRect art, unsigned color, std::uint64_t tick, bool training) {
    float left = 0, top = 0, right = float(stage.source[2]), bottom = float(stage.source[3]);
    for (const auto& layer : stage.layers) if (layer.visible(training)) {
        left = std::min(left, layer.destination[0]); top = std::min(top, layer.destination[1]);
        right = std::max(right, layer.destination[0] + layer.destination[2]);
        bottom = std::max(bottom, layer.destination[1] + layer.destination[3]);
    }
    const float scale = std::min(art.width / (right - left), art.height / (bottom - top));
    const UiRect bounds{art.x + (art.width - (right - left) * scale) * .5f - left * scale,
        art.y + (art.height - (bottom - top) * scale) * .5f - top * scale,
        stage.source[2] * scale, stage.source[3] * scale};
    buildingImage(stage, bounds, color, tick, training);
}

void Renderer::drawBuildingPortrait(const Simulation& game, const Building& building, const SelectionPanelLayout& layout) {
    const auto p = layout.portrait;
    const UiRect art{p.x + 4, p.y + 4, p.width - 8, p.height - 8};
    const auto color = building.owner == game.player().id ? teamColor_ : enemyColor_;
    drawPortraitBackdrop(p, color);
    target_->PushAxisAlignedClip(rect(art.x, art.y, art.width, art.height), D2D1_ANTIALIAS_MODE_ALIASED);
    const auto& type = building.definition;
    if (const auto* stage = type.buildingSprite.stage(type.constructionTicks, type.constructionTicks)) {
        buildingPortrait(*stage, art, color, game.clock().elapsedTicks(), building.training());
    } else {
        // Reuse the world renderer for prototype buildings without authored artwork.
        auto preview = building; preview.constructionProgress = type.constructionTicks;
        const auto original = buildingBounds(game, preview, WorldView{});
        const float scale = std::min(art.width / original.width, art.height / original.height);
        WorldView view{{art.x + art.width * .5f - (original.x + original.width * .5f) * scale,
                        art.y + art.height * .5f - (original.y + original.height * .5f) * scale}, scale};
        buildingSprite(game, preview, view, false);
    }
    target_->PopAxisAlignedClip();
    buttonFrame(p);
    portraitVitals(layout, building.health, type.maximumHealth, 0x9dd7b0);
}

void Renderer::drawCrystalPortrait(const Crystal& crystal, const SelectionPanelLayout& layout) {
    const auto p = layout.portrait;
    const auto& definition = worldAssets_.crystalSprite(crystal.definitionId);
    drawPortraitBackdrop(p, definition.glowColor);
    const auto& frame = definition.variants.front();
    auto& bitmap = worldSprites_[definition.image];
    if (!bitmap) loadBitmap(paths_.asset(definition.image), bitmap, 0, SpriteTeamMask::None, {}, true, true);
    const auto& r = frame.source;
    const float scale = std::min((p.width - 12) / r[2], (p.height - 12) / r[3]);
    const Vec2 extent{r[2] * scale, r[3] * scale};
    target_->PushAxisAlignedClip(rect(p.x + 4, p.y + 4, p.width - 8, p.height - 8), D2D1_ANTIALIAS_MODE_ALIASED);
    sprite(bitmap.Get(), rect(float(r[0]), float(r[1]), float(r[2]), float(r[3])),
        {p.x + (p.width - extent.x) * .5f, p.y + (p.height - extent.y) * .5f}, extent);
    target_->PopAxisAlignedClip();
    buttonFrame(p);
    portraitVitals(layout, crystal.remaining, crystal.capacity, 0xeac2a4);
}

}
