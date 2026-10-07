#pragma once
#include "rts/Types.hpp"

namespace rts {
// Presentation only: rotate source pixels 45 degrees, then halve their vertical
// extent (2:1 ground art). Both axes use the source width to preserve pixel aspect.
// repeatCells is the source width in cells before this projection.
// The origin is global, independent of paint strokes, cells, camera and chunks.
class GroundTextureProjection {
public:
    GroundTextureProjection(float repeatCells, int sourceWidth, bool isometric = true)
        : scale_(repeatCells / sourceWidth), axis_(scale_ * .7071067811865475f), isometric_(isometric) {}

    Vec2 project(Vec2 pixel) const {
        if (!isometric_) return pixel * scale_;
        return {(pixel.x - pixel.y) * axis_, (pixel.x + pixel.y) * axis_ * .5f};
    }
    Vec2 unproject(Vec2 world) const {
        if (!isometric_) return world * (1.0f / scale_);
        return {(world.x * .5f + world.y) / axis_, (world.y - world.x * .5f) / axis_};
    }

private:
    float scale_, axis_;
    bool isometric_;
};
}
