#include "RenderSupport.hpp"
#include "rts/HarvestVisual.hpp"
#include "rts/UnitAnimation.hpp"
#include <numbers>

namespace rts {
using namespace render;
void Renderer::drawHarvestEffects(const Simulation& game, const WorldView& view) {
    const float z = view.zoom;
    const float seconds = float(game.clock().elapsedTicks() % 36000) / Simulation::ticksPerSecond;
    const auto draw = [&](const Entity& unit, const HarvestEffectDefinition& effect) {
        const auto* crystal = harvestEffectTarget(game, unit);
        if (!crystal) return;
        const auto& art = unit.definition.sprite;
        const auto socket = art.workOrigins ? (*art.workOrigins)[unitFrame(unit).row] : Vec2{.5f, .65f};
        const auto unitStart = unitScreenAnchor(view, unit.position, game.unitHeight(unit)) +
            Vec2{(socket.x - art.anchor.x) * art.size.x, (socket.y - art.anchor.y) * art.size.y} * z;
        const auto start = unit.definition.collision == CollisionShape::Footprint ?
            view.project(unit.position, game.unitHeight(unit)) : unitStart;
        const auto finish = view.project(crystal->center(), float(game.map().at(crystal->cell).height)) +
            Vec2{0, -22.f - 10 * (std::max(crystal->width, crystal->height) - 1)} * z;
        const float margin = (effect.arcHeight + 12) * z;
        const auto screen = size();
        if (std::max(start.x, finish.x) < -margin || std::min(start.x, finish.x) > screen.x + margin ||
            std::max(start.y, finish.y) < -margin || std::min(start.y, finish.y) > screen.y + margin) return;
        const float phase = seconds * 4 + float(unit.id % 17);
        const float pulse = .78f + .22f * std::sin(phase);
        const auto at = [&](float t) {
            const float arch = 4 * t * (1 - t);
            return start + (finish - start) * t + Vec2{0, -effect.arcHeight * arch +
                1.5f * std::sin(phase + t * 9) * std::sin(t * std::numbers::pi_v<float>)} * z;
        };
        const auto visibleAt = [&](float t) {
            const auto p = unit.position + (crystal->center() - unit.position) * t;
            return game.fog().visible({int(std::floor(p.x)), int(std::floor(p.y))});
        };
        constexpr int segments = 16;
        for (int i = 0; i < segments; ++i) {
            const float t = float(i) / segments, next = float(i + 1) / segments;
            if (!visibleAt(t) || !visibleAt(next)) continue;
            brush_->SetColor(D2D1::ColorF(effect.color, .13f * pulse));
            target_->DrawLine(point(at(t)), point(at(next)), brush_.Get(), (effect.width + 3) * z);
            brush_->SetColor(D2D1::ColorF(effect.color, .7f * pulse));
            target_->DrawLine(point(at(t)), point(at(next)), brush_.Get(), effect.width * z);
        }
        const auto spark = [&](Vec2 p, float radius, float strength) {
            brush_->SetColor(D2D1::ColorF(effect.color, .10f * strength));
            target_->FillEllipse(D2D1::Ellipse(point(p), radius * 3 * z, radius * 3 * z), brush_.Get());
            brush_->SetColor(D2D1::ColorF(effect.color, .28f * strength));
            target_->FillEllipse(D2D1::Ellipse(point(p), radius * 1.7f * z, radius * 1.7f * z), brush_.Get());
            brush_->SetColor(D2D1::ColorF(0xffedc2, .9f * strength));
            target_->FillEllipse(D2D1::Ellipse(point(p), radius * z, radius * z), brush_.Get());
        };
        spark(start, 1.6f, pulse);
        spark(finish, 2.3f, pulse);
        for (int i = 0; i < 2; ++i) {
            const float t = 1.f - std::fmod(seconds * .65f + float(unit.id % 11) * .071f + i * .5f, 1.f);
            if (visibleAt(t)) spark(at(t), 1.3f, std::sin(t * std::numbers::pi_v<float>));
        }
    };
    for (const auto* entity : game.entities()) if (const auto* ability = entity->harvestAbility())
        for (const auto& entry : ability->effects)
            if (const auto* h = std::get_if<HarvestDefinition>(&entry.value); h && h->effect) draw(*entity, *h->effect);
}
}
