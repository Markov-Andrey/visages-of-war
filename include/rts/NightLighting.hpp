#pragma once
#include "rts/Simulation.hpp"
#include "rts/FogMask.hpp"
#include <span>

namespace rts {
struct ProjectedLight {
    Vec2 position;
    float radius{}, intensity{};
    unsigned color{};
};
float nightStrength(const WorldClock& clock);
// Input/output masks are premultiplied BGRA. White with alpha is the authored convention.
std::uint8_t emissionCoverage(std::uint32_t mask);
std::uint8_t highlightEmission(std::uint32_t pixel);
float lightFalloff(float distanceSquared, float radius);
float flamePulse(const BuildingSpriteLayer& layer, std::uint64_t ticks);
float crystalPulse(EntityId id, std::uint64_t ticks);
std::vector<ProjectedLight> buildingLights(const Simulation& game, const WorldView& view);
std::vector<ProjectedLight> crystalLights(const Simulation& game, const WorldView& view, unsigned color = 0xff713c);

// Small screen-space surface lighting field, composited per surface/sprite in depth order.
class NightLightingRaster {
public:
    static constexpr int pixelStep = 6;
    void update(const Simulation& game, const WorldView& view, Vec2 extent, const FogMask& fog,
        std::span<const ProjectedLight> additionalLights = {}, unsigned crystalColor = 0xff713c);
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<std::uint32_t>& pixels() const { return pixels_; }
    const std::vector<std::uint32_t>& visibilityPixels() const { return visibilityPixels_; }
private:
    int width_{}, height_{};
    std::vector<std::uint32_t> pixels_;
    std::vector<std::uint32_t> visibilityPixels_;
    std::vector<float> visibility_;
};
}
