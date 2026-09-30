#pragma once
#include "rts/Simulation.hpp"
#include "rts/FogMask.hpp"

namespace rts {
struct ProjectedLight {
    Vec2 position;
    float radius{}, intensity{};
    unsigned color{};
};
float nightStrength(const WorldClock& clock);
std::vector<ProjectedLight> buildingLights(const Simulation& game, const WorldView& view);
std::vector<ProjectedLight> crystalLights(const Simulation& game, const WorldView& view);

// Small screen-space overlay. It changes night exposure, never logical visibility.
class NightLightingRaster {
public:
    static constexpr int pixelStep = 6;
    void update(const Simulation& game, const WorldView& view, Vec2 extent, const FogMask& fog);
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<std::uint32_t>& pixels() const { return pixels_; }
private:
    int width_{}, height_{};
    std::vector<std::uint32_t> pixels_;
    std::vector<float> visibility_;
};
}
