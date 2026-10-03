#pragma once
#include "rts/Paths.hpp"
#include "rts/Types.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rts {
// Experimental presentation data. Rows match UnitAnimation: S, SE, E, NE, N, NW, W, SW.
struct SpritePixels {
    int width{}, height{};
    std::vector<std::uint8_t> bgra; // Premultiplied alpha throughout filtering/warping.
};
struct SpriteWarp {
    // Horizontal mesh slices at 0, .2, .4, .6, .8, 1 of canvas height.
    std::array<float, 6> width{1, 1, 1, 1, 1, 1};
    std::array<float, 6> shift{}; // Fractions of canvas width; feet stay fixed.
};
struct DirectionalSourceFrame {
    std::string image;
    std::array<int, 4> source{};
    Vec2 pivot{}; // Source-image coordinates, independent of crop/weapon extents.
    float scale = 1;
};
struct DirectionalSpriteRecipe {
    int width{}, height{};
    Vec2 pivot{};
    // Each source contains stand, four walk frames, four attack frames.
    std::array<std::array<DirectionalSourceFrame, 9>, 2> sources; // SE, NW.
    SpriteWarp south, east, north;
};
DirectionalSpriteRecipe loadDirectionalSpriteRecipe(const Paths& paths, const std::string& recipe);
SpritePixels warpSprite(const SpritePixels& source, const SpriteWarp& warp, float pivotX, bool mirror = false);
SpritePixels assembleDirectionalSprite(const DirectionalSpriteRecipe& recipe,
    const std::array<std::array<SpritePixels, 9>, 2>& frames);
}
