#include "RenderSupport.hpp"
#include "rts/GroundTextureProjection.hpp"
#include <algorithm>
#include <stdexcept>

namespace rts {
using namespace render;

void Renderer::reloadWorldAssets(const WorldAssets& assets) {
    for (const auto& [id, bitmap] : worldIslands_) spriteLights_.erase(bitmap.Get());
    worldIslands_.clear(); worldSourcePixels_.clear();
    directionalSheets_.clear();
    for (const auto& [path, bitmap] : worldSprites_) spriteLights_.erase(bitmap.Get());
    for (const auto& [key, bitmap] : unitSheets_) spriteLights_.erase(bitmap.Get());
    for (const auto& [key, bitmap] : maskedImages_) spriteLights_.erase(bitmap.Get());
    emissionMasks_.clear();
    worldAssets_=assets; materialResources_.clear(); worldSprites_.clear(); unitSheets_.clear(); unitUiImages_.clear(); maskedImages_.clear(); paintResources_.clear(); terrainPaint_.reset();
}
void Renderer::validateWorldAssets(const WorldAssets& assets) {
    std::map<std::filesystem::path, Vec2> dimensionsCache;
    const auto dimensions=[&](const std::filesystem::path& file) {
        if (const auto found = dimensionsCache.find(file); found != dimensionsCache.end()) return found->second;
        ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
        check(wic_->CreateDecoderFromFilename(paths_.asset(file).c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf()));
        check(decoder->GetFrame(0,frame.GetAddressOf())); UINT w{},h{}; check(frame->GetSize(&w,&h));
        if(!w||!h||w>8192||h>8192) throw std::runtime_error("World asset dimensions must be within 8192 pixels");
        const Vec2 size{float(w),float(h)}; dimensionsCache.emplace(file, size); return size;
    };
    for(const auto& m:assets.materials()) dimensions(m.image);
    for(const auto& d:assets.objects()) if(!d.image.empty()) {
        const auto size=dimensions(d.image);
        if(d.source[2]>0&&d.source[3]>0&&(d.source[0]+d.source[2]>size.x||d.source[1]+d.source[3]>size.y)) throw std::runtime_error("Sprite source rectangle exceeds image: "+d.id);
    }
    const auto& bones = assets.bonesSprite();
    for (const auto& [id, path] : assets.resourceIcons()) dimensions(path);
    for (const auto& crystal : assets.crystalSprites()) {
        const auto crystalSize = dimensions(crystal.image);
        for (const auto& frame : crystal.variants)
            if (frame.source[0] + frame.source[2] > crystalSize.x || frame.source[1] + frame.source[3] > crystalSize.y)
                throw std::runtime_error("Crystal variant exceeds image");
    }
    const auto bonesSize = dimensions(bones.image);
    for (const auto& frame : bones.variants)
        if (frame.source[0] + frame.source[2] > bonesSize.x || frame.source[1] + frame.source[3] > bonesSize.y)
            throw std::runtime_error("Bones variant exceeds image");
}
Renderer::MaterialResource& Renderer::materialResource(const std::string& id) {
    auto& r=materialResources_[id]; if(r.bitmap) return r;
    const auto& material=worldAssets_.material(id);
    ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
    check(wic_->CreateDecoderFromFilename(paths_.asset(material.image).c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf()));
    check(decoder->GetFrame(0,frame.GetAddressOf())); check(wic_->CreateFormatConverter(converter.GetAddressOf()));
    check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    UINT w{},h{}; check(converter->GetSize(&w,&h));
    if(!w||!h||w>8192||h>8192) throw std::runtime_error("Invalid terrain texture dimensions");
    r.image.width=int(w); r.image.height=int(h); r.image.repeatCells=material.repeatCells; r.image.pixels.resize(static_cast<size_t>(w)*h);
    r.image.isometric=material.isometric;
    check(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(r.image.pixels.size()*4),reinterpret_cast<BYTE*>(r.image.pixels.data())));
    for(auto& pixel:r.image.pixels) {
        uint32_t color=pixel&0xff000000;
        for(int shift:{0,8,16}) color|=(((pixel>>shift)&255)*((material.tint>>shift)&255)/255)<<shift;
        pixel=color;
    }
    const auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    check(target_->CreateBitmap(D2D1::SizeU(w,h),r.image.pixels.data(),w*4,properties,r.bitmap.GetAddressOf()));
    check(target_->CreateBitmapBrush(r.bitmap.Get(),D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP,D2D1_EXTEND_MODE_WRAP,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR),r.brush.GetAddressOf()));
    const GroundTextureProjection projection(material.repeatCells, int(w), material.isometric);
    const auto xAxis = projection.project({1, 0}), yAxis = projection.project({0, 1});
    r.brush->SetTransform(D2D1::Matrix3x2F(xAxis.x, xAxis.y, yAxis.x, yAxis.y, 0, 0));
    return r;
}
void Renderer::prepareLandscape(const Landscape& landscape,const Map& map) {
    groundBrush_=materialResource(landscape.baseMaterial).brush;
    if(terrainPaint_.update(landscape,map.width(),map.height(),[&](const std::string& id)->const MaterialPixels& { return materialResource(id).image; })) paintResources_.clear();
    const auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    constexpr auto pixels=TerrainPaint::chunkPixels;
    for(const auto& [key,chunk]:terrainPaint_.chunks()) {
        auto& r=paintResources_[key]; if(r.bitmap&&r.revision==chunk.revision) continue;
        if(!r.bitmap) {
            check(target_->CreateBitmap(D2D1::SizeU(pixels,pixels),chunk.pixels.data(),pixels*4,properties,r.bitmap.GetAddressOf()));
            check(target_->CreateBitmapBrush(r.bitmap.Get(),D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP,D2D1_EXTEND_MODE_CLAMP,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR),r.brush.GetAddressOf()));
            r.brush->SetTransform(D2D1::Matrix3x2F::Scale(1.0f/TerrainPaint::pixelsPerCell,1.0f/TerrainPaint::pixelsPerCell)*
                D2D1::Matrix3x2F::Translation(float(key.first*TerrainPaint::chunkCells),float(key.second*TerrainPaint::chunkCells)));
        } else check(r.bitmap->CopyFromMemory(nullptr,chunk.pixels.data(),pixels*4));
        r.revision=chunk.revision;
    }
}
void Renderer::paintedTile(Cell c) {
    const auto at=paintResources_.find({c.x/TerrainPaint::chunkCells,c.y/TerrainPaint::chunkCells});
    if(at==paintResources_.end()) return;
    at->second.brush->SetOpacity(worldOpacity_);
    target_->FillRectangle(rect(float(c.x),float(c.y),1,1),at->second.brush.Get());
}
void Renderer::worldSprite(const WorldObjectDefinition& d,Vec2 position,float scale,float rotation,const Map& map,const WorldView& view) {
    if(d.image.empty()) return;
    const Cell cell{int(position.x),int(position.y)}; if(!map.contains(cell)) return;
    const auto p=view.project(position,map.surfaceHeight(cell,position));
    const auto extent=d.size*(view.zoom*scale); const Vec2 start=p-Vec2{extent.x*d.anchor.x,extent.y*d.anchor.y};
    const float reach=std::max(extent.x,extent.y);
    if(p.x+reach<0||p.y+reach<0||p.x-reach>size().x||p.y-reach>size().y) return;
    auto* bitmap = worldBitmap(d);
    const auto imageSize=bitmap->GetSize();
    const auto source=!d.islandSeed&&d.source[2]>0&&d.source[3]>0?rect(d.source[0],d.source[1],d.source[2],d.source[3]):rect(0,0,imageSize.width,imageSize.height);
    if(source.right>imageSize.width||source.bottom>imageSize.height) throw std::runtime_error("Object sprite rectangle outside image");
    D2D1_MATRIX_3X2_F previous; target_->GetTransform(&previous);
    if(rotation!=0) target_->SetTransform(D2D1::Matrix3x2F::Rotation(rotation,point(p))*previous);
    sprite(bitmap,source,start,extent,d.pixelArt); target_->SetTransform(previous);
}
void Renderer::decoration(const Decoration& d,const Map& map,const WorldView& view) { worldSprite(worldAssets_.object(d.definitionId),d.position,d.scale,d.rotation,map,view); }
}
