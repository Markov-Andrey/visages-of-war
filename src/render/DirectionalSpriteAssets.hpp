#pragma once
#include "rts/DirectionalSprite.hpp"
#include "rts/Weapons.hpp"
#include <wincodec.h>

namespace rts::render {
DirectionalSpriteRecipe directionalRecipe(const Paths& paths, const UnitSpriteDefinition& sprite);
SpritePixels loadDirectionalSprite(const Paths& paths, IWICImagingFactory* wic, const UnitSpriteDefinition& sprite, unsigned color = 0);
}
