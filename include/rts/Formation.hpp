#pragma once
#include "rts/Navigation.hpp"

namespace rts {
struct FormationMember {
    EntityId id; Cell start;
    MovementType movement = MovementType::Walking;
    int priority = 1;
    Vec2 forward{0, 1};
    float radius = .35f;
    std::optional<Vec2> position;
};
struct FormationObstacle { Cell cell; bool air{}; std::optional<Vec2> position; float radius = .35f; };
struct FormationDestination { EntityId id; Cell cell; Vec2 forward; Vec2 position; };
std::vector<FormationDestination> planFormation(const Map& map, std::span<const FormationMember> members,
                                               Cell target, std::span<const FormationObstacle> held);
}
