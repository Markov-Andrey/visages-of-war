#pragma once
#include "rts/Types.hpp"
#include <algorithm>
#include <cmath>

namespace rts {
struct TerrainColumns { int begin{}, end{}; };

// A diagonal has fixed x+y=depth, so projected X is monotonic in column X
// and independent of height. Keep a one-column guard against float rounding;
// the renderer still performs its exact screen/fog rejection before drawing.
inline TerrainColumns terrainColumns(const WorldView& view, int width, int height, int depth,
    float screenLeft, float screenRight) {
    const int begin = std::max(0, depth - height + 1), end = std::min(width, depth + 1);
    if (begin >= end) return {};
    const float scale = WorldView::tileSize * view.zoom;
    const float left = ((screenLeft - view.origin.x) / scale + depth) * .5f;
    const float right = ((screenRight - view.origin.x) / scale + depth) * .5f;
    return {int(std::clamp(std::floor(left) - 1, float(begin), float(end))),
        int(std::clamp(std::ceil(right) + 1, float(begin), float(end)))};
}
}
