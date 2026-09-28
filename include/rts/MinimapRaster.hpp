#pragma once
#include "rts/FogMask.hpp"

namespace rts {
// Static terrain + fog only. Units, resource markers and camera outlines draw
// separately every frame so this cache never delays moving markers.
class MinimapRaster {
public:
    static constexpr unsigned resolution = 164;
    bool update(const Map& map, const FogMask& fog);
    const std::vector<uint32_t>& pixels() const { return pixels_; }
private:
    int width_{}, height_{};
    const FogMask* fog_{};
    uint64_t fogRevision_{};
    std::vector<uint32_t> terrain_, pixels_;
};
}
