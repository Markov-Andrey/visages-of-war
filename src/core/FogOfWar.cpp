#include "rts/FogOfWar.hpp"
#include <algorithm>
#include <cmath>

namespace rts {
bool visionReaches(const Map& map, VisionSource source, Cell target) {
    if (!map.contains(source.cell) || !map.contains(target)) return false;
    const Cell delta = target - source.cell;
    if (groundLengthSquared({float(delta.x), float(delta.y)}) > source.radius * source.radius) return false;
    if (source.air) return true;
    const int height = map.at(source.cell).height;
    Cell sample = source.cell;
    const int sx = delta.x < 0 ? -1 : 1, sy = delta.y < 0 ? -1 : 1;
    const int ax = std::abs(delta.x), ay = std::abs(delta.y);
    int crossedX = 0, crossedY = 0;
    const auto opaque = [&](Cell c) { return map.at(c).height > height || map.blocksVision(c); };
    while (sample != target) {
        // Traverse every cell crossed by the centre-to-centre ray. At an exact
        // corner, two touching obstacles close the gap; a single corner does not.
        const int crossing = (1 + 2 * crossedX) * ay - (1 + 2 * crossedY) * ax;
        if (crossing == 0) {
            if (opaque(sample + Cell{sx, 0}) && opaque(sample + Cell{0, sy})) return false;
            sample.x += sx; sample.y += sy; ++crossedX; ++crossedY;
        } else if (crossing < 0) { sample.x += sx; ++crossedX; }
        else { sample.y += sy; ++crossedY; }
        if (map.at(sample).height > height) return false;
        // The first opaque object is visible, but its shadow is not.
        if (sample != target && map.blocksVision(sample)) return false;
    }
    return true;
}
FogOfWar::FogOfWar(int width, int height) : width_(width), height_(height),
    cells_(static_cast<size_t>(width) * height, Visibility::Unexplored) {}
Visibility FogOfWar::at(Cell cell) const {
    if (cell.x < 0 || cell.y < 0 || cell.x >= width_ || cell.y >= height_) return Visibility::Unexplored;
    return cells_[static_cast<size_t>(cell.y) * width_ + cell.x];
}
void FogOfWar::revealAll() {
    revealed_ = true;
    std::fill(cells_.begin(), cells_.end(), Visibility::Visible);
}
void FogOfWar::update(const Map& map, std::span<const VisionSource> sources) {
    if (revealed_) return;
    for (auto& cell : cells_) if (cell == Visibility::Visible) cell = Visibility::Explored;
    for (const auto& source : sources) {
        if (!map.contains(source.cell)) continue;
        const int extent = int(std::ceil(groundRadiusExtent(float(source.radius))));
        for (int y = std::max(0, source.cell.y - extent); y <= std::min(height_ - 1, source.cell.y + extent); ++y) {
            const int dy = y - source.cell.y;
            // A circular ground-plane radius is an ellipse in cell coordinates.
            const float squared = .64f * (source.radius * source.radius - dy * dy);
            if (squared < 0) continue;
            const float middle = source.cell.x + .6f * dy, halfWidth = std::sqrt(squared);
            for (int x = std::max(0, int(std::ceil(middle - halfWidth - .0001f)));
                 x <= std::min(width_ - 1, int(std::floor(middle + halfWidth + .0001f))); ++x) {
                auto& cell = cells_[static_cast<size_t>(y) * width_ + x];
                // Visibility is the union of all observers. A ray cannot add anything
                // to a cell already revealed during this update.
                if (cell != Visibility::Visible && visionReaches(map, source, {x, y})) cell = Visibility::Visible;
            }
        }
    }
}
}
