#pragma once
#include "rts/Types.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace rts {
enum class AttackTargets { SameLayer, Ground, Air, All };
enum class ProjectileTargeting { Unit, Point };
enum class AttackPhase { Ready, Windup, Recovery, Cooldown };
enum class SpriteTeamMask { None, Blue, Purple };
struct UnitDeathFrame {
    std::array<int, 4> source{};
    Vec2 anchor; // Ground pivot in pixels relative to this source rectangle.
};
struct UnitDeathSprite {
    std::string image;
    std::string teamMask; // Optional authored paint mask; preserves the source alpha.
    std::vector<UnitDeathFrame> frames;
    float scale = 1; // Screen pixels per source pixel at zoom 1.
    int ticksPerFrame = 6;
};
struct UnitSpriteDefinition {
    std::string image = "sprites/worker.png";
    std::string portrait; // Optional standalone HUD artwork; empty uses the idle icon.
    std::string icon; // Optional button artwork; independent of the large portrait.
    std::string portraitMask, iconMask; // Optional team paint for the respective UI artwork.
    std::string directionRecipe; // Optional two-view stand/walk/attack synthesis recipe.
    bool pixelArt = true;
    int frameWidth = 32, frameHeight = 32;
    Vec2 size{80, 80}, anchor{.5f, .7125f};
    std::array<int, 8> rows{0, 1, 2, 3, 4, 5, 6, 7}; // S, SE, E, NE, N, NW, W, SW.
    std::vector<int> walk{0, 2, 1, 3}, windup{4, 5}, recovery{6, 7};
    float walkCycleDistance = 1; // Logical cells travelled per full walk animation cycle.
    int idle{};
    SpriteTeamMask teamMask = SpriteTeamMask::Blue;
    std::optional<UnitDeathSprite> death; // One shared sequence, final frame persists as a corpse.
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
