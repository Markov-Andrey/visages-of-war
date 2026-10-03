#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::drawHealthBar(const Unit& unit, UiRect bounds, std::uint64_t tick, unsigned border) {
    if (unit.health <= 0 || bounds.width <= 0 || bounds.height <= 0) return;
    const int maximum = std::max(1, unit.maximumHealth());
    const float health = std::clamp(float(unit.health), 0.0f, float(maximum));
    const float trail = std::min(float(maximum), health + unit.healthFeedback.remaining(tick));
    const float pixelsPerHealth = bounds.width / maximum;
    const auto antialias = target_->GetAntialiasMode();
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    const auto fill = [&](UiRect area, unsigned color) {
        brush_->SetColor(D2D1::ColorF(color));
        target_->FillRectangle(rect(area.x, area.y, area.width, area.height), brush_.Get());
    };
    // Ownership stays visible in the world border; red inside means recent damage.
    fill({bounds.x - 1, bounds.y - 1, bounds.width + 2, bounds.height + 2}, border);
    fill(bounds, 0x080f14);
    if (trail > health)
        fill({bounds.x + health * pixelsPerHealth, bounds.y, (trail - health) * pixelsPerHealth, bounds.height}, 0xec4b48);
    fill({bounds.x, bounds.y, health * pixelsPerHealth, bounds.height}, 0x75dc91);
    // Keep every 100-HP boundary, even at small zoom. A partial last section keeps
    // its true width (e.g. 150 HP = 100 + 50), without adding an end divider.
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float dividerWidth = std::min(1.0f, 100 * pixelsPerHealth * .3f);
    for (int hp = 100; hp < maximum; hp += 100) {
        const float x = bounds.x + hp * pixelsPerHealth;
        const float left = dividerWidth == 1 ? std::floor(x) : x - dividerWidth * .5f;
        fill({left, bounds.y, dividerWidth, bounds.height}, 0x080f14);
    }
    target_->SetAntialiasMode(antialias);
}
}
