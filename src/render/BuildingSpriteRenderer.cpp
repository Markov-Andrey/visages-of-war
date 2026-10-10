#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::buildingImage(const BuildingSpriteStage& stage, UiRect bounds, unsigned color, std::uint64_t ticks, bool training, bool visible, bool complete) {
    const auto draw = [&](const auto& art, const std::array<int, 4>& source, UiRect destination) {
        const auto& image = art.image;
        const auto& teamMask = art.teamMask;
        auto* emission = visible && nightActive_ && !art.emissionMask.empty() ? emissionBitmap(art.emissionMask) : nullptr;
        sprite(maskedBitmap(image, teamMask, color), rect(float(source[0]), float(source[1]), float(source[2]), float(source[3])),
            {destination.x, destination.y}, {destination.width, destination.height}, false, visible && art.emissive ? 0.0f : 1.0f, emission);
    };
    draw(stage, stage.source, bounds);
    for (size_t index = 0; index < stage.layers.size(); ++index) {
        const auto& layer = stage.layers[index];
        if (!layer.visible(training) || (!complete && layer.emissive)) continue;
        const auto destination = buildingLayerBounds(stage, layer, bounds);
        if (visible && layer.emissive) for (const auto& light : stage.lights)
            if (light.animationLayer == static_cast<int>(index) && light.visible(training)) {
                const auto position = Vec2{bounds.x + light.position.x * bounds.width / stage.source[2],
                    bounds.y + light.position.y * bounds.height / stage.source[3]};
                drawLightGlow(position, std::min(destination.width, destination.height) * .8f, light.color,
                    light.intensity * (1 - light.flicker * flamePulse(layer, ticks)));
            }
        draw(layer, layer.frame(ticks), destination);
    }
}
}
