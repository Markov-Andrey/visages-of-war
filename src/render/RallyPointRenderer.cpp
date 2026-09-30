#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::drawRallyPoint(const Simulation& game, const Building& building, const WorldView& view) {
    // Ownership remains mandatory even if a caller supplies a foreign selection.
    if (building.owner != game.player().id || building.owner == neutralPlayer) return;
    const auto p = view.project(center(building.rally), game.map().surfaceHeight(building.rally, center(building.rally)));
    if (const auto found = rallySprites_.find(game.player().commanderId); found != rallySprites_.end()) {
        const auto& art = found->second;
        const auto& frame = art.frame(game.clock().elapsedTicks());
        const auto& source = frame.source;
        const float scale = art.scale * view.zoom;
        sprite(maskedBitmap(art.image, art.teamMask, teamRgb(game.player().color)),
            rect(float(source[0]), float(source[1]), float(source[2]), float(source[3])),
            p - frame.anchor * scale, Vec2{float(source[2]), float(source[3])} * scale);
    } else {
        line(p, p + Vec2{0, -55} * view.zoom, 0xe1ddbb, 2);
        const std::array<Vec2, 3> flag{{p + Vec2{0, -55} * view.zoom, p + Vec2{31, -44} * view.zoom, p + Vec2{0, -33} * view.zoom}};
        polygon(flag, teamRgb(game.player().color));
    }
}
}
