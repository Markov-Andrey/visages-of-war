#pragma once
#include "rts/Types.hpp"
#include "rts/Match.hpp"
#include <string>
#include <vector>

namespace rts {
struct PaintStamp {
    std::string material;
    Vec2 position;
    float radius = 1, opacity = .6f, hardness = .4f;
    bool erase{};
    bool operator==(const PaintStamp&) const = default;
};
struct Decoration {
    EntityId id{};
    std::string definitionId;
    Vec2 position;
    float scale = 1, rotation{};
};
struct Landscape {
    std::string baseMaterial = "grass";
    std::vector<PaintStamp> paint;
    std::vector<Decoration> decorations;
};
}
