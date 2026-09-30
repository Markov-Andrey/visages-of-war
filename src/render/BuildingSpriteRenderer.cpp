#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::buildingImage(const BuildingSpriteStage& stage, UiRect bounds, unsigned color, std::uint64_t ticks, bool training) {
    const auto draw = [&](const std::string& image, const std::string& teamMask, const std::array<int, 4>& source, UiRect destination) {
        const auto path = imagePath(image), mask = imagePath(teamMask);
        const auto tint = mask.empty() ? 0 : color;
        auto& bitmap = buildingImages_[{path, mask, tint}];
        if (!bitmap) loadBitmap(paths_.asset(path), bitmap, tint, SpriteTeamMask::None,
            mask.empty() ? std::filesystem::path{} : paths_.asset(mask));
        sprite(bitmap.Get(), rect(float(source[0]), float(source[1]), float(source[2]), float(source[3])),
            {destination.x, destination.y}, {destination.width, destination.height});
    };
    draw(stage.image, stage.teamMask, stage.source, bounds);
    for (const auto& layer : stage.layers) if (layer.visible(training))
        draw(layer.image, layer.teamMask, layer.frame(ticks), buildingLayerBounds(stage, layer, bounds));
}
}
