#pragma once
#include "rts/FogOfWar.hpp"
#include <cstdint>

namespace rts {
// Presentation only. Never use the feathered values for targeting or selection.
class FogMask {
public:
    static constexpr int pixelsPerCell = 4;
    static constexpr int padding = 1;
    bool update(const FogOfWar& fog, int width, int height);
    int pixelWidth() const { return (width_ + padding * 2) * pixelsPerCell; }
    int pixelHeight() const { return (height_ + padding * 2) * pixelsPerCell; }
    const std::vector<uint32_t>& pixels() const { return pixels_; }
    uint64_t revision() const { return revision_; }
    // Half-open pixel bounds changed by the most recent update; empty on a cache hit.
    struct PixelRegion { int left{}, top{}, right{}, bottom{}; };
    PixelRegion changedRegion() const { return changed_; }
    float lightAt(Vec2 world) const;
    bool covers(Cell cell) const;
private:
    int width_{}, height_{};
    uint64_t revision_{};
    PixelRegion changed_;
    std::vector<Visibility> cells_;
    std::vector<Visibility> scratch_;
    std::vector<float> light_;
    std::vector<uint32_t> pixels_;
};
}
