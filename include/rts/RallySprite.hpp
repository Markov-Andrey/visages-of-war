#pragma once
#include "rts/Types.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rts {
struct RallySpriteFrame {
    std::array<int, 4> source{};
    Vec2 anchor{}; // Ground point in source pixels, relative to this frame.
};
struct RallySpriteLight {
    Vec2 offset{}; // Source pixels relative to the ground anchor, shared by all frames.
    float radius = 1.2f, intensity = .7f;
    unsigned color = 0xffc478;
};
struct RallySpriteDefinition {
    std::string image, teamMask;
    std::vector<RallySpriteFrame> frames;
    float scale = .14f;
    int ticksPerFrame = 4;
    std::optional<RallySpriteLight> light;
    const RallySpriteFrame& frame(std::uint64_t ticks) const {
        return frames.at((ticks / ticksPerFrame) % frames.size());
    }
};
}
