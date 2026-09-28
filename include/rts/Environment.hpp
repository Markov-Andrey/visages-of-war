#pragma once
#include "rts/Match.hpp"
#include "rts/Types.hpp"
#include <string>
#include <vector>

namespace rts {
enum class EnvironmentKind { Tree, Rock, Arch, Decoration };
enum class Interaction { None, Inspect, Destructible };
struct EnvironmentObject {
    EntityId id{};
    EnvironmentKind kind{};
    Cell origin{};
    int width{};
    int height{};
    // Row-major occupancy, independent of the sprite's extent or transparent pixels.
    std::vector<bool> collision;
    Interaction interaction = Interaction::None;
    int hitPoints{};
    int maximumHitPoints{};
    bool blocksVision{};
    std::string definitionId;
    bool active() const { return hitPoints > 0; }
    bool blocks(int x, int y) const { return collision.at(static_cast<size_t>(y) * width + x); }
    bool occludes(int x, int y) const { return active() && blocksVision && blocks(x, y); }
};
EnvironmentObject makeEnvironment(const std::string& type, EntityId id, Cell origin);
struct ObjectDestroyed { EntityId id; EnvironmentKind kind; };
struct ObjectRestored { EntityId id; EnvironmentKind kind; };
}
