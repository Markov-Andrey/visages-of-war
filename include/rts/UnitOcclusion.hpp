#pragma once
#include "rts/GameplayUi.hpp"

namespace rts {
// A padded body-sized window, with a smooth fringe in logical screen pixels.
// Shared by the opacity mask and picking; no simulation or fog state is changed.
struct UnitOcclusion {
    UiRect body;
    float padding, feather;
    UiRect bounds() const {
        const float r = padding + feather;
        return {body.x - r, body.y - r, body.width + 2*r, body.height + 2*r};
    }
    float distance(Vec2 p) const {
        const float dx = std::max({body.x - p.x, 0.f, p.x - body.x - body.width});
        const float dy = std::max({body.y - p.y, 0.f, p.y - body.y - body.height});
        return std::sqrt(dx*dx + dy*dy);
    }
    bool contains(Vec2 p) const { return distance(p) <= padding; }
    float opacity(Vec2 p) const {
        const float t = std::clamp((distance(p) - padding) / feather, 0.f, 1.f);
        return .18f + .82f * t*t*(3 - 2*t);
    }
};
inline UnitOcclusion unitOcclusion(const Simulation& game, const Unit& unit, const WorldView& view) {
    return {unitBounds(game, unit, view), 5 * view.zoom, 14 * view.zoom};
}
inline bool occlusionEligible(const Simulation& game, const Unit& unit, float depth) {
    return unit.health > 0 && game.fog().visible(unit.cell) && !airborne(unit.definition.movement) &&
        unitDrawDepth(unit.position) < depth;
}
}
