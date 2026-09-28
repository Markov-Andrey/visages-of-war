#pragma once
#include "rts/Map.hpp"
#include <span>

namespace rts {
enum class Visibility : unsigned char { Unexplored, Explored, Visible };
struct VisionSource { Cell cell; int radius; bool air{}; };
bool visionReaches(const Map& map, VisionSource source, Cell target);
class FogOfWar {
public:
    FogOfWar(int width, int height);
    void update(const Map& map, std::span<const VisionSource> sources);
    Visibility at(Cell cell) const;
    bool visible(Cell cell) const { return at(cell) == Visibility::Visible; }
    bool explored(Cell cell) const { return at(cell) != Visibility::Unexplored; }
private:
    int width_, height_;
    std::vector<Visibility> cells_;
};
}
