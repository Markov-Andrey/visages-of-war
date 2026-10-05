#pragma once
#include "rts/Types.hpp"

namespace rts {
// Presentation at the fixed simulation tick: steering and collision never read this state.
class MotionFacing {
public:
    void reset();
    Cell update(Cell current, Vec2 displacement, float nominalStep);
private:
    Vec2 average_{};
    Cell pending_{};
    int pendingTicks_{};
    bool initialized_{};
};
}
