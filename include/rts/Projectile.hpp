#pragma once
#include "rts/Match.hpp"

namespace rts {
struct Projectile {
    ProjectileDefinition definition;
    AttackTargets targets{};
    EntityId target{};
    PlayerId owner{};
    bool sourceAir{};
    int damage{}, elapsedTicks{}, flightTicks{};
    Vec2 start{}, aim{}, position{}, previousPosition{};
    float startHeight{}, aimHeight{}, height{}, previousHeight{};
};
struct ProjectileImpact { Vec2 position; float height{}, radius{}; int remainingTicks = 12; };
}
