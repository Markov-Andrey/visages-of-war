#include "rts/ConstructionVisual.hpp"
#include <algorithm>
#include <cmath>
#include <queue>

namespace rts {
float constructionSmooth(float low, float high, float value) {
    const float t = std::clamp((value - low) / (high - low), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
ConstructionPhase constructionPhase(float progress) {
    const float p = std::clamp(progress, 0.f, 1.f);
    if (p >= 1) return {0, 1, 1, 0};
    return {1 - constructionSmooth(.03f, .33f, p), std::min(1.f, p / .33f),
        std::clamp((p - .33f) / .67f, 0.f, 1.f), 1 - constructionSmooth(.96f, 1.f, p)};
}
namespace {
std::vector<float> blur(const std::vector<float>& a, int w, int h, int radius) {
    std::vector<float> temp(a.size()), result(a.size());
    const float n = float(radius * 2 + 1);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float sum = 0; for (int k = -radius; k <= radius; ++k) sum += a[y*w + std::clamp(x+k, 0, w-1)];
        temp[y*w+x] = sum/n;
    }
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float sum = 0; for (int k = -radius; k <= radius; ++k) sum += temp[std::clamp(y+k, 0, h-1)*w+x];
        result[y*w+x] = sum/n;
    }
    return result;
}
SpritePixels colorize(const std::vector<float>& a, int w, int h, unsigned color) {
    SpritePixels image{w,h,std::vector<std::uint8_t>(size_t(w)*h*4)};
    for (size_t i=0;i<a.size();++i) {
        const auto alpha = static_cast<unsigned>(std::lround(std::clamp(a[i],0.f,1.f)*255));
        for (int c=0;c<3;++c) image.bgra[i*4+c] = static_cast<std::uint8_t>(((color>>(c*8))&255)*alpha/255);
        image.bgra[i*4+3] = static_cast<std::uint8_t>(alpha);
    }
    return image;
}
float coverage(const SpritePixels& image, int i) {
    return (.11f*image.bgra[i*4]+.59f*image.bgra[i*4+1]+.30f*image.bgra[i*4+2])/255;
}
}
ConstructionPixels makeConstructionPixels(const SpritePixels& source, const SpritePixels* contours, const SpritePixels* order) {
    const int w=source.width, h=source.height;
    if (w<3 || h<3 || source.bgra.size()!=size_t(w)*h*4) throw std::invalid_argument("Invalid construction source");
    for (const auto* mask : {contours,order}) if (mask && (mask->width!=w || mask->height!=h || mask->bgra.size()!=source.bgra.size()))
        throw std::invalid_argument("Construction masks must match the sprite");
    std::vector<float> lum(size_t(w)*h), alpha(lum.size()), edges(lum.size());
    for (size_t i=0;i<lum.size();++i) {
        alpha[i]=source.bgra[i*4+3]/255.f;
        lum[i]=alpha[i]>0?coverage(source,int(i))/alpha[i]:0;
    }
    if (contours) {
        for (size_t i=0;i<edges.size();++i) edges[i]=coverage(*contours,int(i));
    } else {
        lum=blur(lum,w,h,1);
        std::vector<float> mag(lum.size()), dx(lum.size()), dy(lum.size()), candidate(lum.size());
        for(int y=1;y<h-1;++y) for(int x=1;x<w-1;++x) {
            const int i=y*w+x;
            dx[i]=lum[i-w+1]+2*lum[i+1]+lum[i+w+1]-lum[i-w-1]-2*lum[i-1]-lum[i+w-1];
            dy[i]=lum[i+w-1]+2*lum[i+w]+lum[i+w+1]-lum[i-w-1]-2*lum[i-w]-lum[i-w+1];
            mag[i]=std::hypot(dx[i],dy[i]);
        }
        for(int y=2;y<h-2;++y) for(int x=2;x<w-2;++x) {
            const int i=y*w+x; if(alpha[i]<.9f) continue;
            float angle=std::atan2(dy[i],dx[i])*57.29578f; if(angle<0) angle+=180;
            const int step=angle<22.5f||angle>=157.5f?1:angle<67.5f?w+1:angle<112.5f?w:w-1;
            if(mag[i]>=mag[i-step] && mag[i]>=mag[i+step] && mag[i]>.19f) candidate[i]=std::clamp((mag[i]-.12f)/.43f,0.f,1.f);
            if(alpha[i]>.95f && (alpha[i-1]<.8f||alpha[i+1]<.8f||alpha[i-w]<.8f||alpha[i+w]<.8f)) candidate[i]=1;
        }
        std::vector<bool> seen(lum.size()); std::queue<int> queue; std::vector<int> component;
        for(int i=0;i<int(lum.size());++i) if(!seen[i] && candidate[i]>0) {
            queue.push(i);seen[i]=true;component.clear();float peak=0;
            while(!queue.empty()) {
                const int j=queue.front();queue.pop();component.push_back(j);peak=std::max(peak,candidate[j]);
                for(int v=-1;v<=1;++v) for(int u=-1;u<=1;++u) {
                    const int x=j%w+u,y=j/w+v;if(x<0||x>=w||y<0||y>=h)continue;
                    const int k=y*w+x;if(!seen[k]&&candidate[k]>0){seen[k]=true;queue.push(k);}
                }
            }
            if(component.size()>=9 && peak>.65f) for(int j:component)edges[j]=candidate[j];
        }
    }
    ConstructionPixels result; result.padding=24; result.imageWidth=w; result.imageHeight=h;
    const int pad=result.padding,pw=w+2*pad,ph=h+2*pad;
    std::vector<float> padded(size_t(pw)*ph), solid(padded.size());
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {padded[(y+pad)*pw+x+pad]=edges[y*w+x];solid[(y+pad)*pw+x+pad]=alpha[y*w+x];}
    auto near=blur(padded,pw,ph,2),far=blur(blur(padded,pw,ph,5),pw,ph,5);
    for(size_t i=0;i<near.size();++i) near[i]=std::clamp(near[i]*1.5f+far[i]*2.5f,0.f,.8f);
    result.lines=colorize(padded,pw,ph,0xfff0b2);
    result.halo=colorize(near,pw,ph,0xffc45c);
    result.surface=colorize(solid,pw,ph,0xffefb0);
    // Cached disjoint rank bands; progress only changes which geometries are drawn.
    // Include transparent padding so the halo is never clipped to the silhouette.
    const int cell=2;
    const auto rank = [&](int x,int y) {
        x=std::clamp(x-pad,0,w-1);y=std::clamp(y-pad,0,h-1);
        const float value=order?coverage(*order,y*w+x):
            1-y/float(h-1)+.008f*std::sin(x*.048f)*std::sin(y*.044f)+.005f*std::sin(x*.014f+y*.036f);
        return std::clamp(int(value*constructionBands),0,constructionBands-1);
    };
    for(int y=0;y<ph;y+=cell) for(int x=0;x<pw;) {
        const int first=x,band=rank(x+cell/2,y+cell/2);
        do{x+=cell;}while(x<pw && rank(x+cell/2,y+cell/2)==band);
        result.regions[band].push_back({float(first-pad),float(y-pad),float(std::min(x,pw)-first),float(std::min(cell,ph-y))});
    }
    return result;
}
}
