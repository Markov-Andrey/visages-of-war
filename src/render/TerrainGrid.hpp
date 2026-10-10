#pragma once
#include "rts/FogMask.hpp"
#include "rts/Map.hpp"

namespace rts::render {
inline bool terrainCellVisible(const Map& map, Cell cell, const WorldView& view, Vec2 extent, const FogMask* fog) {
    const auto p = view.project(center(cell), float(map.at(cell).height));
    return p.x > -250 && p.x < extent.x + 250 && p.y > -100 && p.y < extent.y + 180 && (!fog || fog->covers(cell));
}

// The foreground tile owns a shared flat edge. Keep both sides of cliffs and
// ramps: those edges need not coincide, and their painter order is significant.
inline bool terrainGridEdge(const Map& map, Cell cell, size_t edge, const WorldView& view, Vec2 extent, const FogMask* fog) {
    if (edge != 1 && edge != 2) return true;
    const Cell next = cell + (edge == 1 ? Cell{1, 0} : Cell{0, 1});
    if (!map.contains(next)) return true;
    const auto& a = map.at(cell);
    const auto& b = map.at(next);
    return a.height != b.height || a.ramp != Cell{} || b.ramp != Cell{} || !terrainCellVisible(map, next, view, extent, fog);
}

// Preserve the original fog samples exactly; only join adjacent samples whose
// light is identical. No averaging across an explored/unexplored boundary.
template<class Draw>
void boundaryRuns(Vec2 a, Vec2 b, const FogMask* fog, Draw&& draw) {
    const int count = fog ? FogMask::pixelsPerCell : 1;
    int begin = 0;
    float light = fog ? fog->lightAt(a + (b - a) * (.5f / count)) : 1.f;
    for (int i = 1; i <= count; ++i) {
        const float next = i < count && fog ? fog->lightAt(a + (b - a) * ((i + .5f) / count)) : -1.f;
        if (next == light) continue;
        if (light > 0) draw(float(begin) / count, float(i) / count, light);
        begin = i;
        light = next;
    }
}
}
