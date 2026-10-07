#include "rts/TerrainPaint.hpp"
#include "rts/GroundTextureProjection.hpp"
#include <algorithm>
#include <stdexcept>

namespace rts {
namespace {
// Match the wrapped, linearly filtered bitmap brush used by base materials.
// Pixel centres are at n + .5; inverse projection can yield negative coordinates.
uint32_t sampleMaterial(const MaterialPixels& image, Vec2 pixel) {
    const float px = pixel.x - .5f, py = pixel.y - .5f;
    const int x = int(std::floor(px)), y = int(std::floor(py));
    const float fx = px - x, fy = py - y;
    const auto wrap = [](int value, int size) { const int r = value % size; return r < 0 ? r + size : r; };
    const auto at = [&](int sx, int sy) { return image.pixels[static_cast<size_t>(wrap(sy, image.height)) * image.width + wrap(sx, image.width)]; };
    const uint32_t a = at(x, y), b = at(x + 1, y), c = at(x, y + 1), d = at(x + 1, y + 1);
    uint32_t result = 0;
    for (int shift : {0, 8, 16, 24}) {
        const float top = ((a >> shift) & 255) * (1 - fx) + ((b >> shift) & 255) * fx;
        const float bottom = ((c >> shift) & 255) * (1 - fx) + ((d >> shift) & 255) * fx;
        result |= static_cast<uint32_t>(std::clamp(std::lround(top * (1 - fy) + bottom * fy), 0L, 255L)) << shift;
    }
    return result;
}
}
void TerrainPaint::reset() { applied_.clear(); chunks_.clear(); width_=height_=0; }
bool TerrainPaint::update(const Landscape& landscape,int width,int height,const std::function<const MaterialPixels&(const std::string&)>& image) {
    const bool resetRequired=width!=width_||height!=height_||landscape.paint.size()<applied_.size()||
        !std::equal(applied_.begin(),applied_.end(),landscape.paint.begin());
    if(resetRequired) reset();
    width_=width; height_=height;
    for(size_t i=applied_.size();i<landscape.paint.size();++i) stamp(landscape.paint[i],width,height,image(landscape.paint[i].material));
    if(applied_.size()!=landscape.paint.size()) applied_=landscape.paint;
    return resetRequired;
}
void TerrainPaint::stamp(const PaintStamp& s,int width,int height,const MaterialPixels& image) {
    if(image.width<1||image.height<1||image.repeatCells<=0||image.pixels.size()!=static_cast<size_t>(image.width)*image.height) throw std::runtime_error("Invalid paint material image");
    const GroundTextureProjection projection(image.repeatCells, image.width, image.isometric);
    const int left=std::max(0,int(std::floor((s.position.x-s.radius)*pixelsPerCell))), top=std::max(0,int(std::floor((s.position.y-s.radius)*pixelsPerCell)));
    const int right=std::min(width*pixelsPerCell,int(std::ceil((s.position.x+s.radius)*pixelsPerCell))), bottom=std::min(height*pixelsPerCell,int(std::ceil((s.position.y+s.radius)*pixelsPerCell)));
    for(int cy=top/chunkPixels;cy<=(bottom-1)/chunkPixels;++cy) for(int cx=left/chunkPixels;cx<=(right-1)/chunkPixels;++cx) {
        auto& chunk=chunks_[{cx,cy}]; if(chunk.pixels.empty()) chunk.pixels.resize(chunkPixels*chunkPixels);
        ++chunk.revision;
        for(int y=std::max(top,cy*chunkPixels);y<std::min(bottom,(cy+1)*chunkPixels);++y)
            for(int x=std::max(left,cx*chunkPixels);x<std::min(right,(cx+1)*chunkPixels);++x) {
                const float wx=(x+.5f)/pixelsPerCell,wy=(y+.5f)/pixelsPerCell;
                const float distance=std::hypot(wx-s.position.x,wy-s.position.y)/s.radius;
                if(distance>=1) continue;
                float coverage=1;
                if(distance>s.hardness) { const float t=(1-distance)/std::max(.001f,1-s.hardness); coverage=t*t*(3-2*t); }
                const float amount=coverage*s.opacity;
                auto& dest=chunk.pixels[static_cast<size_t>(y-cy*chunkPixels)*chunkPixels+x-cx*chunkPixels];
                uint32_t color=0;
                if(s.erase) {
                    for(int shift:{0,8,16,24}) color|=static_cast<uint32_t>(std::lround(((dest>>shift)&255)*(1-amount)))<<shift;
                } else {
                    const uint32_t source=sampleMaterial(image, projection.unproject({wx, wy}));
                    const float alpha=((source>>24)&255)/255.0f*amount;
                    for(int shift:{0,8,16,24}) {
                        const float value=((source>>shift)&255)*amount+((dest>>shift)&255)*(1-alpha);
                        color|=static_cast<uint32_t>(std::clamp(std::lround(value),0L,255L))<<shift;
                    }
                }
                dest=color;
            }
    }
}
}
