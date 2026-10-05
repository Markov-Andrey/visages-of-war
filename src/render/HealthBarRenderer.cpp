#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::drawHealthBar(const Unit& unit, UiRect bounds, std::uint64_t tick, unsigned border, bool beveled) {
    drawHealthBar(unit.health, unit.maximumHealth(), unit.healthFeedback.remaining(tick), bounds, border, beveled);
}

void Renderer::drawHealthBar(int current, int maximum, float recentDamage, UiRect bounds, unsigned border, bool beveled) {
    if (current <= 0) return;
    drawVitalBar(current, maximum, recentDamage, bounds, border, beveled, false);
}

void Renderer::drawManaBar(const Unit& unit, UiRect bounds, bool beveled) {
    if (unit.maximumMana() <= 0 || unit.health <= 0) return;
    drawVitalBar(unit.mana, unit.maximumMana(), 0, bounds, 0x315595, beveled, true);
}

void Renderer::drawVitalBar(int current, int maximum, float recentDamage, UiRect bounds, unsigned border, bool beveled, bool mana) {
    if (maximum <= 0 || bounds.width <= 0 || bounds.height <= 0) return;
    auto& gradients = mana ? manaBarGradients_ : healthBarGradients_;
    const unsigned fillColor = mana ? 0x699fff : friendlySelectionColor;
    const float health = std::clamp(float(current), 0.0f, float(maximum));
    const float trail = std::min(float(maximum), health + std::max(0.f, recentDamage));
    const float pixelsPerHealth = bounds.width / maximum;
    const auto antialias = target_->GetAntialiasMode();
    if (beveled) {
        // Empty, healthy and recently damaged sections share the same geometry.
        // The sharp midpoint stop forms the facet between light top and dark bottom.
        constexpr unsigned colors[3][5]{
            {0x233329, 0x18261e, 0x16231b, 0x080f0b, 0x0d1811},
            {0x96e4ab, friendlySelectionColor, 0x63bb7b, 0x366543, 0x468457},
            {0xffa08a, 0xf46550, 0xe84c40, 0x952a2b, 0xbc3932}
        };
        constexpr unsigned manaColors[3][5]{
            {0x27354c, 0x1b2941, 0x172338, 0x080f1c, 0x111d31},
            {0xafd5ff, 0x699fff, 0x548be5, 0x2a4a90, 0x3865bd},
            {0xafd5ff, 0x699fff, 0x548be5, 0x2a4a90, 0x3865bd}
        };
        constexpr float stopsAt[]{0.f, .12f, .49f, .50f, 1.f};
        for (size_t i = 0; i < gradients.size(); ++i) {
            auto& gradient = gradients[i];
            if (!gradient) {
                D2D1_GRADIENT_STOP stops[5];
                for (size_t j = 0; j < std::size(stops); ++j)
                    stops[j] = {stopsAt[j], D2D1::ColorF(mana ? manaColors[i][j] : colors[i][j])};
                ComPtr<ID2D1GradientStopCollection> collection;
                check(target_->CreateGradientStopCollection(stops, UINT(std::size(stops)), D2D1_GAMMA_2_2,
                    D2D1_EXTEND_MODE_CLAMP, collection.GetAddressOf()));
                check(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(), D2D1::Point2F(0, 1)), collection.Get(), gradient.GetAddressOf()));
            }
            gradient->SetStartPoint(point({bounds.x, bounds.y}));
            gradient->SetEndPoint(point({bounds.x, bounds.y + bounds.height}));
        }
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const float edge = std::min(.55f, bounds.height * .1f);
        const float radius = std::min(bounds.height * .28f, bounds.width * .5f);
        auto frameColor = D2D1::ColorF(border);
        frameColor.r *= .25f; frameColor.g *= .25f; frameColor.b *= .25f;
        brush_->SetColor(frameColor);
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect(bounds.x - edge, bounds.y - edge,
            bounds.width + edge * 2, bounds.height + edge * 2), radius + edge, radius + edge), brush_.Get());

        const float gap = std::min(.45f, bounds.height * .07f);
        const float healthEnd = bounds.x + health * pixelsPerHealth;
        const float trailEnd = bounds.x + trail * pixelsPerHealth;
        for (int first = 0; first < maximum; first += 100) {
            const int last = std::min(first + 100, maximum);
            const float sectionWidth = (last - first) * pixelsPerHealth;
            // Keep the allocated width proportional even for a tiny final remainder.
            const float inset = std::min(gap, sectionWidth * .12f);
            const float left = bounds.x + first * pixelsPerHealth + (first == 0 ? 0 : inset);
            const float right = bounds.x + last * pixelsPerHealth - (last == maximum ? 0 : inset);
            const bool cap = first == 0 || last == maximum;
            const float rounding = std::min(bounds.height * (cap ? .28f : .16f), (right - left) * .5f);
            const auto section = D2D1::RoundedRect(rect(left, bounds.y, right - left, bounds.height), rounding, rounding);
            target_->FillRoundedRectangle(section, gradients[0].Get());
            const auto paint = [&](float from, float to, ID2D1Brush* fill) {
                const float start = std::max(left, from), end = std::min(right, to);
                if (end <= start) return;
                // Clip the fill, not the section shape: partial HP and draining damage
                // move continuously through a section without changing its end caps.
                target_->PushAxisAlignedClip(rect(start, bounds.y, end - start, bounds.height), D2D1_ANTIALIAS_MODE_ALIASED);
                target_->FillRoundedRectangle(section, fill);
                target_->PopAxisAlignedClip();
            };
            paint(healthEnd, trailEnd, gradients[2].Get());
            paint(bounds.x, healthEnd, gradients[1].Get());
        }
        target_->SetAntialiasMode(antialias);
        return;
    }
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
    fill({bounds.x, bounds.y, health * pixelsPerHealth, bounds.height}, fillColor);
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
