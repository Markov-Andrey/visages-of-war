#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::drawMinimap(const Simulation& game, const BattleLayout& layout, const WorldView& view) {
    const auto mini = layout.minimap;
    brush_->SetColor(D2D1::ColorF(0x31464c));
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect(mini.x - 3, mini.y - 3, mini.width + 6, mini.height + 6), 6, 6), brush_.Get());
    const MinimapProjection miniView(mini, game.map());
    constexpr auto miniPixels = MinimapRaster::resolution;
    const bool changed = minimapRaster_.update(game.map(), fogMask_);
    const auto& pixels = minimapRaster_.pixels();
    if (!minimap_) {
        const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        check(target_->CreateBitmap(D2D1::SizeU(miniPixels, miniPixels), pixels.data(), miniPixels * 4, properties, minimap_.GetAddressOf()));
    } else if (changed) check(minimap_->CopyFromMemory(nullptr, pixels.data(), miniPixels * 4));
    sprite(minimap_.Get(), rect(0, 0, float(miniPixels), float(miniPixels)), {mini.x, mini.y}, {mini.width, mini.height}, true);
    const auto dot = [&](Vec2 world, unsigned color, float radius) {
        const auto p = miniView.project(world);
        brush_->SetColor(D2D1::ColorF(color));
        target_->FillRectangle(rect(p.x - radius, p.y - radius, radius * 2, radius * 2), brush_.Get());
    };
    for (size_t i = 0; i < game.crystals().size(); ++i) if (game.knownCrystal(i) > 0) dot(center(game.crystals()[i].cell), 0xc6a1ee, 1.5f);
    for (const auto& b : game.buildings()) dot({b.origin.x + b.definition.width * .5f, b.origin.y + b.definition.height * .5f}, teamColor_, 3);
    for (const auto& u : game.units()) if (u.owner == game.player().id || game.fog().visible(u.cell))
        dot(u.position, u.owner == game.player().id ? teamColor_ : enemyColor_, 1.5f);
    const auto viewport = miniView.viewport(view, layout.world);
    polygon(viewport, 0xd1e8e0, .8f, false);
    text(L"64 × 64  /  Home — база", rect(mini.x, size().y - 19, 200, 18), 0x6e9395);
}
}
