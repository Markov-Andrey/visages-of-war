#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::drawFloatingNotifications(const Simulation& game, const WorldView& view, const FloatingNotifications& notifications) {
    const auto now = game.clock().elapsedTicks();
    for (const auto& entry : notifications.entries()) {
        if (!entry.visibleTo(game.player().id, game.fog())) continue;
        const float opacity = entry.opacity(now);
        if (opacity <= 0) continue;
        const float t = entry.progress(now);
        const float rise = 34 * (1 - (1 - t) * (1 - t));
        const auto p = unitScreenAnchor(view, entry.position, entry.height) + Vec2{-28, -68 * view.zoom - rise};
        worldOpacity_ = opacity;
        const bool icon = !entry.resourceIcon.empty();
        if (icon) drawResourceIcon(entry.resourceIcon, {p.x, p.y, 20, 20});
        const float textX = p.x + (icon ? 24 : 0);
        brush_->SetColor(D2D1::ColorF(0x241407, opacity));
        target_->DrawTextW(entry.text.c_str(), UINT32(entry.text.size()), bodyFormat_.Get(),
            rect(textX + 1, p.y + 2, 160, 30), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        brush_->SetColor(D2D1::ColorF(entry.color, opacity));
        target_->DrawTextW(entry.text.c_str(), UINT32(entry.text.size()), bodyFormat_.Get(),
            rect(textX, p.y + 1, 160, 30), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    worldOpacity_ = 1;
}
}
