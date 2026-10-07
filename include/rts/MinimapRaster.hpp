#pragma once
#include "rts/FogMask.hpp"

namespace rts {
class Simulation;
// Terrain and last-known environment footprints. Dynamic markers draw separately.
class MinimapRaster {
public:
    static constexpr unsigned resolution = 164;
    bool update(const Simulation& game, const FogMask& mask);
    bool update(const Map& map, const FogOfWar& fog, const FogMask& mask,
                std::span<const EnvironmentObject* const> knownObstacles = {});
    const std::vector<uint32_t>& pixels() const { return pixels_; }
private:
    int width_{}, height_{};
    const FogMask* fog_{};
    uint64_t fogRevision_{};
    std::optional<Map> terrainMap_; // Occupancy-free geometry: never expose live hidden objects.
    std::vector<unsigned char> edges_;
    std::vector<uint32_t> cells_, scratch_, pixels_;
};
}
