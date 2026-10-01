#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::drawNightLighting(const Simulation& game, const WorldView& view, Vec2 extent, const GameplayUi& ui) {
    if (nightStrength(game.clock()) <= 0 || extent.x <= 0 || extent.y <= 0) return;
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
    nightLighting_.update(game, view, extent, fogMask_, rallyLights);
    const auto w = static_cast<UINT32>(nightLighting_.width()), h = static_cast<UINT32>(nightLighting_.height());
    const auto previous = nightBitmap_ ? nightBitmap_->GetPixelSize() : D2D1_SIZE_U{};
    if (!nightBitmap_ || previous.width != w || previous.height != h) {
        const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        check(target_->CreateBitmap(D2D1::SizeU(w, h), nightLighting_.pixels().data(), w * 4, properties, nightBitmap_.ReleaseAndGetAddressOf()));
    } else check(nightBitmap_->CopyFromMemory(nullptr, nightLighting_.pixels().data(), w * 4));
    sprite(nightBitmap_.Get(), rect(0, 0, float(w), float(h)), {0, 0}, extent);
}
}
