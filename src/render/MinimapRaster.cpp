#include "rts/MinimapRaster.hpp"
#include "rts/Minimap.hpp"
#include <algorithm>

namespace rts {
bool MinimapRaster::update(const Map& map, const FogMask& fog) {
    bool changed = width_ != map.width() || height_ != map.height() ||
        fog_ != &fog || fogRevision_ != fog.revision() || pixels_.empty();
    width_ = map.width(); height_ = map.height();
    terrain_.resize(static_cast<size_t>(width_) * height_);
    // Map::at permits direct editing. Compare the visible terrain properties so
    // same-size replacement maps and in-place edits cannot leave a stale image.
    for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) {
        const auto& tile = map.at({x, y});
        const unsigned color = tile.surface == Surface::ShallowWater ? 0x4e9fa5 :
            tile.surface == Surface::DeepWater ? 0x245879 : tile.height > 0 ? 0x839575 : 0x516749;
        auto& previous = terrain_[static_cast<size_t>(y) * width_ + x];
        changed |= previous != color;
        previous = color;
    }
    if (!changed) return false;
    pixels_.assign(resolution * resolution, 0xff101c25);
    const MinimapProjection raster({0, 0, float(resolution), float(resolution)}, map);
    for (unsigned y = 0; y < resolution; ++y) for (unsigned x = 0; x < resolution; ++x) {
        const Vec2 sample{x + .5f, y + .5f};
        const auto cell = raster.pick(sample);
        if (!cell) continue;
        const unsigned color = terrain_[static_cast<size_t>(cell->y) * width_ + cell->x];
        const float light = fog.lightAt(raster.unproject(sample));
        unsigned shaded = 0xff000000;
        for (unsigned shift : {0, 8, 16})
            shaded |= static_cast<unsigned>(std::lerp(float((0x081119 >> shift) & 255), float((color >> shift) & 255), light)) << shift;
        pixels_[static_cast<size_t>(y) * resolution + x] = shaded;
    }
    fog_ = &fog; fogRevision_ = fog.revision();
    return true;
}
}
