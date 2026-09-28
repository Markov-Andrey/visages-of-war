#pragma once
#include "rts/Navigation.hpp"

namespace rts {
struct FormationMember {
    EntityId id; Cell start;
    MovementType movement = MovementType::Walking;
    int priority = 1;
    Vec2 forward{0, 1};
};
struct FormationObstacle { Cell cell; bool air{}; };
struct FormationDestination { EntityId id; Cell cell; Vec2 forward; };
std::vector<FormationDestination> planFormation(const Map& map, std::span<const FormationMember> members,
                                               Cell target, std::span<const FormationObstacle> held);
}
