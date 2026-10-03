#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::prepareNightLighting(const Simulation& game, const WorldView& view, Vec2 extent, const GameplayUi& ui) {
    nightAmount_ = nightStrength(game.clock());
    nightActive_ = nightAmount_ > 0 && extent.x > 0 && extent.y > 0;
    if (!nightActive_) return;
    std::vector<ProjectedLight> rallyLights;
    const auto art = rallySprites_.find(game.player().commanderId);
    if (art != rallySprites_.end() && art->second.light && art->second.light->intensity > 0) {
        const auto& light = *art->second.light;
        for (const auto& building : game.buildings()) {
            if (!rallyPointLightVisible(game, building, ui)) continue;
            const auto ground = view.project(center(building.rally), game.map().surfaceHeight(building.rally, center(building.rally)));
            rallyLights.push_back({ground + light.offset * (art->second.scale * view.zoom),
                light.radius * WorldView::tileSize * view.zoom, light.intensity, light.color});
        }
    }
    nightLighting_.update(game, view, extent, fogMask_, rallyLights, worldAssets_.crystalSprite().glowColor);
    const auto w = static_cast<UINT32>(nightLighting_.width()), h = static_cast<UINT32>(nightLighting_.height());
    const auto previous = nightBitmap_ ? nightBitmap_->GetPixelSize() : D2D1_SIZE_U{};
    if (!nightBitmap_ || previous.width != w || previous.height != h) {
        const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        check(target_->CreateBitmap(D2D1::SizeU(w, h), nightLighting_.pixels().data(), w * 4, properties, nightBitmap_.ReleaseAndGetAddressOf()));
        check(target_->CreateBitmapBrush(nightBitmap_.Get(), nightBrush_.ReleaseAndGetAddressOf()));
        check(target_->CreateBitmap(D2D1::SizeU(w, h), nightLighting_.visibilityPixels().data(), w * 4, properties, glowVisibility_.ReleaseAndGetAddressOf()));
        check(target_->CreateBitmapBrush(glowVisibility_.Get(), glowVisibilityBrush_.ReleaseAndGetAddressOf()));
    } else {
        check(nightBitmap_->CopyFromMemory(nullptr, nightLighting_.pixels().data(), w * 4));
        check(glowVisibility_->CopyFromMemory(nullptr, nightLighting_.visibilityPixels().data(), w * 4));
    }
    nightBrush_->SetTransform(D2D1::Matrix3x2F::Scale(extent.x / w, extent.y / h));
    glowVisibilityBrush_->SetTransform(D2D1::Matrix3x2F::Scale(extent.x / w, extent.y / h));
    if (!spriteLightLayer_) check(target_->CreateLayer(spriteLightLayer_.GetAddressOf()));
}
void Renderer::drawLightGlow(Vec2 position, float radius, unsigned color, float intensity) {
    if (!nightActive_ || radius <= 0 || intensity <= 0) return;
    auto& glow = glowBrushes_[color];
    if (!glow) {
        const D2D1_GRADIENT_STOP stops[]{{0, D2D1::ColorF(color, 1)}, {.25f, D2D1::ColorF(color, .65f)},
            {.65f, D2D1::ColorF(color, .12f)}, {1, D2D1::ColorF(color, 0)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        check(target_->CreateGradientStopCollection(stops, 4, collection.GetAddressOf()));
        check(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(point({0, 0}), point({0, 0}), 1, 1),
            collection.Get(), glow.GetAddressOf()));
    }
    glow->SetTransform(D2D1::Matrix3x2F::Scale(radius, radius) * D2D1::Matrix3x2F::Translation(position.x, position.y));
    const auto bounds = rect(position.x - radius, position.y - radius, radius * 2, radius * 2);
    target_->PushLayer(D2D1::LayerParameters(bounds, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), worldOpacity_ * nightAmount_ * intensity * .18f, glowVisibilityBrush_.Get()), spriteLightLayer_.Get());
    target_->FillEllipse(D2D1::Ellipse(point(position), radius, radius), glow.Get());
    target_->PopLayer();
}
void Renderer::lightSurface(std::span<const Vec2> points) {
    if (!nightActive_ || points.size() < 3) return;
    ComPtr<ID2D1PathGeometry> geometry;
    ComPtr<ID2D1GeometrySink> sink;
    check(factory_->CreatePathGeometry(geometry.GetAddressOf()));
    check(geometry->Open(sink.GetAddressOf()));
    sink->BeginFigure(point(points.front()), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < points.size(); ++i) sink->AddLine(point(points[i]));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    check(sink->Close());
    const auto antialias = target_->GetAntialiasMode();
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    nightBrush_->SetOpacity(worldOpacity_);
    target_->FillGeometry(geometry.Get(), nightBrush_.Get());
    target_->SetAntialiasMode(antialias);
}
}
