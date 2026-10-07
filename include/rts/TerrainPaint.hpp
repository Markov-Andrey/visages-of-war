#pragma once
#include "rts/Landscape.hpp"
#include <cstdint>
#include <functional>
#include <map>

namespace rts {
struct MaterialPixels { int width{}, height{}; float repeatCells{}; std::vector<uint32_t> pixels; bool isometric = true; };
struct PaintChunk { std::vector<uint32_t> pixels; uint64_t revision{}; };
// CPU cache for visual paint only. Sparse chunks avoid a giant texture per map/material.
class TerrainPaint {
public:
    static constexpr int pixelsPerCell=32, chunkCells=8, chunkPixels=pixelsPerCell*chunkCells;
    using Key=std::pair<int,int>;
    bool update(const Landscape& landscape,int width,int height,const std::function<const MaterialPixels&(const std::string&)>& image);
    const auto& chunks() const { return chunks_; }
    void reset();
private:
    void stamp(const PaintStamp& stamp,int width,int height,const MaterialPixels& image);
    std::vector<PaintStamp> applied_;
    std::map<Key,PaintChunk> chunks_;
    int width_{},height_{};
};
}
