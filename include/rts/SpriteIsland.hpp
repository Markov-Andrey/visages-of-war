#pragma once
#include "rts/DirectionalSprite.hpp"

namespace rts {
// Select one source silhouette without changing or repacking the original PNG.
SpritePixels extractSpriteIsland(const SpritePixels& sheet, std::array<int, 4> source, Cell seed);
}
