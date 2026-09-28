#pragma once
#include "rts/Types.hpp"
#include <array>
#include <string>
#include <vector>

namespace rts {
enum class AttackTargets { SameLayer, Ground, Air, All };
enum class ProjectileTargeting { Unit, Point };
enum class AttackPhase { Ready, Windup, Recovery, Cooldown };
enum class SpriteTeamMask { None, Blue, Purple };
struct UnitSpriteDefinition {
    std::string image = "sprites/worker.png";
    int frameWidth = 32, frameHeight = 32;
    Vec2 size{80, 80}, anchor{.5f, .7125f};
    std::array<int, 8> rows{0, 1, 2, 3, 4, 5, 6, 7}; // S, SE, E, NE, N, NW, W, SW.
    std::vector<int> walk{0, 2, 1, 3}, windup{4, 5}, recovery{6, 7};
    int idle{};
    SpriteTeamMask teamMask = SpriteTeamMask::Blue;
};
struct ProjectileDefinition {
    ProjectileTargeting targeting = ProjectileTargeting::Unit;
    std::string image;
    std::array<int, 4> source{}; // x, y, width, height; zero size = entire PNG.
    Vec2 size{32, 8};
    float rotationOffset{};
    float speed = 10; // Logical cells/second; determines the flight duration at launch.
    float arcHeight = 1; // Peak above the straight line, in terrain elevation units.
    float launchHeight = 1.5f, impactHeight = 1;
    float splashRadius{};
    bool friendlyFire{};
};
}
