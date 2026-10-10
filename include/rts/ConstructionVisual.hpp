#pragma once
#include "rts/SpriteIsland.hpp"
#include <array>

namespace rts {
inline constexpr int constructionBands = 128;
float constructionSmooth(float low, float high, float value);
struct ConstructionPhase {
    float invocation{}, lines{}, material{}, glow{};
};
ConstructionPhase constructionPhase(float progress);
// Target-independent, cached per artwork. White masks use luminance times alpha.
struct ConstructionPixels {
    SpritePixels lines, halo, surface;
    int padding{}, imageWidth{}, imageHeight{};
    std::array<std::vector<std::array<float, 4>>, constructionBands> regions;
};
ConstructionPixels makeConstructionPixels(const SpritePixels& source,
    const SpritePixels* contours = nullptr, const SpritePixels* order = nullptr);
}
