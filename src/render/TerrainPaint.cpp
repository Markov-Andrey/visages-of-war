#include "rts/TerrainPaint.hpp"
#include <algorithm>
#include <stdexcept>

namespace rts {
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
                    const int sx=int(std::floor(wx/image.repeatCells*image.width))%image.width,sy=int(std::floor(wy/image.repeatCells*image.height))%image.height;
                    const uint32_t source=image.pixels[static_cast<size_t>(sy)*image.width+sx];
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
