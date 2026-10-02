#pragma once
#include "rts/Types.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rts {
enum class BuildingLayerWhen { Always, Training };
struct BuildingSpriteLight {
    Vec2 position{}; // Source pixels relative to the stage crop, like layer destinations.
    float radius = 3; // Logical cells; scaled by camera zoom, independently of PNG resolution.
    float intensity = .85f, flicker = .04f;
    unsigned color = 0xffbc70;
    int animationLayer = -1; // Optional flame layer driving flicker, in the stage's layer array.
    BuildingLayerWhen when = BuildingLayerWhen::Always;
    bool visible(bool training) const { return when == BuildingLayerWhen::Always || training; }
};
struct BuildingSpriteLayer {
    std::string image, teamMask, emissionMask;
    bool emissive = false;
    std::vector<std::array<int, 4>> frames;
    // Rectangle relative to the stage's cropped source, in source pixels.
    std::array<float, 4> destination{};
    int ticksPerFrame = 3, phase{};
    BuildingLayerWhen when = BuildingLayerWhen::Always;
    bool visible(bool training) const { return when == BuildingLayerWhen::Always || training; }
    const std::array<int, 4>& frame(std::uint64_t ticks) const {
        return frames.at((ticks / ticksPerFrame % frames.size() + phase) % frames.size());
    }
};
struct BuildingSpriteStage {
    int from{}; // Inclusive construction percentage; 100 is the finished building.
    std::string image, teamMask, emissionMask;
    bool emissive = false;
    std::array<int, 4> source{};
    Vec2 anchor{.5f, .8f}; // Ground point within the source rectangle.
    std::vector<BuildingSpriteLayer> layers; // Drawn over the base, in authored order.
    std::vector<BuildingSpriteLight> lights; // Presentation only; never adds fog observers.
};
struct BuildingSpriteDefinition {
    float scale = 1; // Display pixels per source pixel at zoom 1; never affects occupancy.
    std::vector<BuildingSpriteStage> stages;
    const BuildingSpriteStage* stage(int progress, int duration) const {
        const BuildingSpriteStage* result = nullptr;
        for (const auto& candidate : stages) {
            if (std::int64_t(progress) * 100 < std::int64_t(candidate.from) * duration) break;
            result = &candidate;
        }
        return result;
    }
};
}
