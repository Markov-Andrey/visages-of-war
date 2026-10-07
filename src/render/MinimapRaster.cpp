#include "rts/MinimapRaster.hpp"
#include "rts/Minimap.hpp"
#include "rts/Simulation.hpp"
#include <algorithm>
#include <array>

namespace rts {
namespace {
constexpr unsigned unseen = 0x081119;
constexpr unsigned solid = 1u << 24;
constexpr std::array<Cell, 4> directions{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
unsigned obstacleColor(EnvironmentKind kind) {
    switch (kind) {
    case EnvironmentKind::Tree: return 0x293c36;
    case EnvironmentKind::Rock: return 0x655d62;
    default: return 0x65503e;
    }
}
unsigned shade(unsigned color, float light) {
    unsigned shaded = 0xff000000;
    for (unsigned shift : {0, 8, 16})
        shaded |= static_cast<unsigned>(std::lerp(float((unseen >> shift) & 255), float((color >> shift) & 255), light)) << shift;
    return shaded;
}
}
bool MinimapRaster::update(const Simulation& game, const FogMask& mask) {
    std::vector<const EnvironmentObject*> known;
    for (size_t i = 0; i < game.environment().size(); ++i)
        if (game.knownEnvironment(i)) known.push_back(&game.environment()[i]);
    return update(game.map(), game.fog(), mask, known);
}
bool MinimapRaster::update(const Map& map, const FogOfWar& fog, const FogMask& mask,
                           std::span<const EnvironmentObject* const> knownObstacles) {
    bool geometryChanged = !terrainMap_ || width_ != map.width() || height_ != map.height();
    bool changed = geometryChanged || fog_ != &mask || fogRevision_ != mask.revision() || pixels_.empty();
    width_ = map.width(); height_ = map.height();
    if (geometryChanged) terrainMap_.emplace(width_, height_);
    const size_t count = static_cast<size_t>(width_) * height_;
    scratch_.resize(count);
    for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) {
        const Cell cell{x, y};
        const auto& tile = map.at(cell);
        auto& previous = terrainMap_->at(cell);
        geometryChanged |= tile.height != previous.height || tile.surface != previous.surface ||
            tile.blocked != previous.blocked || tile.ramp != previous.ramp;
        previous = tile;
        // Logical exploration gates the raster; feathering must not reveal terrain.
        const auto visibility = fog.at(cell);
        unsigned color = tile.blocked ? 0x3b3740 : tile.surface == Surface::DeepWater ? 0x243d50 :
            tile.surface == Surface::ShallowWater ? 0x54787e : tile.height > 0 ? 0x9a9b80 : 0x879172;
        if (tile.blocked || tile.surface == Surface::DeepWater) color |= solid;
        scratch_[static_cast<size_t>(y) * width_ + x] = visibility == Visibility::Unexplored ? 0 :
            color | (static_cast<unsigned>(visibility) << 25);
    }
    // Objects in this list represent remembered presence, even if destroyed out
    // of sight. Never consult active(), live occupancy, or hidden enemy units.
    for (const auto* object : knownObstacles) {
        const auto color = obstacleColor(object->kind);
        for (int y = 0; y < object->height; ++y) for (int x = 0; x < object->width; ++x) {
            const Cell cell = object->origin + Cell{x, y};
            if (!map.contains(cell) || !object->blocks(x, y) || !fog.explored(cell)) continue;
            auto& value = scratch_[static_cast<size_t>(cell.y) * width_ + cell.x];
            value = (value & 0xfe000000u) | solid | color;
        }
    }
    if (geometryChanged) {
        edges_.assign(count, 0);
        for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) {
            const Cell cell{x, y};
            for (size_t edge = 0; edge < directions.size(); ++edge) {
                const Cell next = cell + directions[edge];
                if (!map.contains(next)) continue;
                const auto& a = terrainMap_->at(cell);
                const auto& b = terrainMap_->at(next);
                if ((a.height != b.height || a.ramp != Cell{} || b.ramp != Cell{}) &&
                    !terrainMap_->canStep(cell, next))
                    edges_[static_cast<size_t>(y) * width_ + x] |= static_cast<unsigned char>(1u << edge);
            }
        }
    }
    changed |= geometryChanged || scratch_ != cells_;
    if (!changed) return false;
    cells_.swap(scratch_);
    pixels_.assign(resolution * resolution, 0xff101c25);
    const MinimapProjection raster({0, 0, float(resolution), float(resolution)}, map);
    const float pixelsPerCell = float(resolution) / std::max(width_, height_);
    const float edgeWidth = std::min(.3f, .7f / pixelsPerCell);
    const auto lightAt = [&](Cell cell, Vec2 world) {
        // Retain enough contrast for remembered paths; visible areas stay brighter.
        return fog.visible(cell) ? std::clamp(mask.lightAt(world), .7f, 1.0f) : .6f;
    };
    for (unsigned y = 0; y < resolution; ++y) for (unsigned x = 0; x < resolution; ++x) {
        const Vec2 sample{x + .5f, y + .5f};
        const auto cell = raster.pick(sample);
        if (!cell) continue;
        auto& pixel = pixels_[static_cast<size_t>(y) * resolution + x];
        if (!fog.explored(*cell)) { pixel = 0xff000000 | unseen; continue; }
        const auto index = static_cast<size_t>(cell->y) * width_ + cell->x;
        unsigned color = cells_[index] & 0xffffff;
        const Vec2 world = raster.unproject(sample);
        const float fx = world.x - cell->x, fy = world.y - cell->y;
        const std::array<bool, 4> nearEdge{{fx > 1 - edgeWidth, fy > 1 - edgeWidth, fx < edgeWidth, fy < edgeWidth}};
        for (size_t edge = 0; edge < directions.size(); ++edge)
            if ((edges_[index] & (1u << edge)) && nearEdge[edge] && fog.explored(*cell + directions[edge]))
                color = 0x45423f;
        pixel = shade(color, lightAt(*cell, world));
    }
    // A single blocked cell on a large map must not fall between raster samples.
    // Minimum-size marks represent only explored obstructions, never hidden ones.
    if (pixelsPerCell < 1) for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) {
        const auto value = cells_[static_cast<size_t>(y) * width_ + x];
        if (!(value & solid)) continue;
        const Cell cell{x, y};
        const auto p = raster.project(center(cell));
        const int px = std::clamp(int(p.x), 0, int(resolution) - 1), py = std::clamp(int(p.y), 0, int(resolution) - 1);
        pixels_[static_cast<size_t>(py) * resolution + px] = shade(value & 0xffffff, lightAt(cell, center(cell)));
    }
    fog_ = &mask; fogRevision_ = mask.revision();
    return true;
}
}
