#include "RenderSupport.hpp"

namespace rts {
using namespace render;
namespace {
SpritePixels readConstructionImage(IWICImagingFactory* wic, const std::filesystem::path& path,
    std::array<int,4> crop, int width, int height) {
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    Microsoft::WRL::ComPtr<IWICBitmapClipper> clip;
    Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    check(wic->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf()));
    check(decoder->GetFrame(0,frame.GetAddressOf()));
    check(wic->CreateBitmapClipper(clip.GetAddressOf()));
    const WICRect area{crop[0],crop[1],crop[2],crop[3]};
    check(clip->Initialize(frame.Get(),&area));
    check(wic->CreateBitmapScaler(scaler.GetAddressOf()));
    check(scaler->Initialize(clip.Get(),UINT(width),UINT(height),WICBitmapInterpolationModeFant));
    check(wic->CreateFormatConverter(converter.GetAddressOf()));
    check(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    SpritePixels result{width,height,std::vector<std::uint8_t>(size_t(width)*height*4)};
    check(converter->CopyPixels(nullptr,UINT(width*4),UINT(result.bgra.size()),result.bgra.data()));
    return result;
}
}
Renderer::ConstructionResource& Renderer::constructionResource(const EntityDefinition& type) {
    auto& resource=constructionResources_[type.id];
    if(resource.lines) return resource;
    auto& pixels=constructionPixels_[type.id];
    const auto& stage=type.buildingSprite.stages.back();
    const auto& effect=*type.buildingSprite.construction;
    if(pixels.lines.bgra.empty()) {
        const int width=512,height=std::max(3,int(std::lround(512.f*stage.source[3]/stage.source[2])));
        auto source=readConstructionImage(wic_.Get(),paths_.asset(imagePath(stage.image)),stage.source,width,height);
        // Include static architecture (e.g. the hall dome), not animated flame frames.
        for(const auto& layer:stage.layers) if(!layer.emissive && layer.when==BuildingLayerWhen::Always) {
            const auto& d=layer.destination;
            const int x0=int(std::lround(d[0]*width/stage.source[2])),y0=int(std::lround(d[1]*height/stage.source[3]));
            const int w=std::max(1,int(std::lround(d[2]*width/stage.source[2]))),h=std::max(1,int(std::lround(d[3]*height/stage.source[3])));
            const auto overlay=readConstructionImage(wic_.Get(),paths_.asset(imagePath(layer.image)),layer.frames.front(),w,h);
            for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
                if(x+x0<0||x+x0>=width||y+y0<0||y+y0>=height)continue;
                const size_t a=(size_t(y)*w+x)*4,b=(size_t(y+y0)*width+x+x0)*4;
                for(int c=0;c<4;++c) source.bgra[b+c]=static_cast<BYTE>(overlay.bgra[a+c]+source.bgra[b+c]*(255-overlay.bgra[a+3])/255);
            }
        }
        SpritePixels contours,order;
        if(!effect.contours.empty()) contours=readConstructionImage(wic_.Get(),paths_.asset(imagePath(effect.contours)),stage.source,width,height);
        if(!effect.revealMask.empty()) order=readConstructionImage(wic_.Get(),paths_.asset(imagePath(effect.revealMask)),stage.source,width,height);
        pixels=makeConstructionPixels(source,effect.contours.empty()?nullptr:&contours,effect.revealMask.empty()?nullptr:&order);
    }
    const auto upload=[&](const SpritePixels& image,ComPtr<ID2D1Bitmap>& bitmap) {
        check(target_->CreateBitmap(D2D1::SizeU(UINT(image.width),UINT(image.height)),image.bgra.data(),UINT(image.width*4),
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),bitmap.GetAddressOf()));
    };
    upload(pixels.lines,resource.lines);upload(pixels.halo,resource.halo);upload(pixels.surface,resource.surface);
    std::array<ID2D1Geometry*,constructionBands> bands{};
    for(int i=0;i<constructionBands;++i) {
        check(factory_->CreatePathGeometry(resource.bands[i].GetAddressOf()));
        ComPtr<ID2D1GeometrySink> sink;check(resource.bands[i]->Open(sink.GetAddressOf()));
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        for(const auto& r:pixels.regions[i]) {
            sink->BeginFigure({r[0],r[1]},D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine({r[0]+r[2],r[1]});sink->AddLine({r[0]+r[2],r[1]+r[3]});sink->AddLine({r[0],r[1]+r[3]});
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        }
        check(sink->Close());bands[i]=resource.bands[i].Get();
    }
    for(int i=0;i<=constructionBands;++i) {
        if(i)check(factory_->CreateGeometryGroup(D2D1_FILL_MODE_WINDING,bands.data(),UINT(i),resource.before[i].GetAddressOf()));
        if(i<constructionBands)check(factory_->CreateGeometryGroup(D2D1_FILL_MODE_WINDING,bands.data()+i,UINT(constructionBands-i),resource.after[i].GetAddressOf()));
    }
    return resource;
}
void Renderer::constructionImage(const Simulation& game,const Building& building,const WorldView& view,UiRect bounds,unsigned color,bool visible,float progress) {
    const auto& type=building.definition;
    const auto& effect=*type.buildingSprite.construction;
    const auto& stage=type.buildingSprite.stages.back();
    const auto phase=constructionPhase(progress);
    if(phase.invocation>0) {
        auto* image=maskedBitmap(effect.image,{},0);const auto size=image->GetSize();
        const auto ground=view.project({building.origin.x+type.width*.5f,building.origin.y+type.height*.5f},float(game.map().at(building.origin).height));
        const float width=(type.width+type.height)*WorldView::tileSize*view.zoom*effect.footprintScale,height=width*size.height/size.width;
        target_->DrawBitmap(image,rect(ground.x-width*effect.anchor.x,ground.y-height*effect.anchor.y,width,height),
            phase.invocation*worldOpacity_*(visible?1.f:.5f),D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    if(phase.lines<=0) return;
    auto& resource=constructionResource(type);const auto& pixels=constructionPixels_.at(type.id);
    const float sx=bounds.width/pixels.imageWidth,sy=bounds.height/pixels.imageHeight;
    const auto mapping=D2D1::Matrix3x2F::Scale(sx,sy)*D2D1::Matrix3x2F::Translation(bounds.x,bounds.y);
    const auto expanded=rect(bounds.x-pixels.padding*sx,bounds.y-pixels.padding*sy,
        pixels.lines.width*sx,pixels.lines.height*sy);
    const auto push=[&](ID2D1Geometry* geometry,float opacity) {
        target_->PushLayer(D2D1::LayerParameters(expanded,geometry,D2D1_ANTIALIAS_MODE_ALIASED,mapping,opacity),nullptr);
    };
    // Rank regions are fixed. Only two neighbouring bands cross-fade at each boundary.
    const auto reveal=[&](float amount,const auto& draw) {
        const float level=std::clamp(amount,0.f,1.f)*constructionBands;
        const int whole=std::min(constructionBands,int(level));
        if(whole>0){push(resource.before[whole].Get(),1);draw();target_->PopLayer();}
        if(whole<constructionBands && level>whole) {push(resource.bands[whole].Get(),level-whole);draw();target_->PopLayer();}
    };
    const float materialLevel=phase.material*constructionBands;
    const int solid=std::min(constructionBands,int(materialLevel));
    const float partial=materialLevel-solid;
    if(phase.material>0) reveal(phase.material,[&]{buildingImage(stage,bounds,color,game.clock().elapsedTicks(),false,visible,false);});
    const auto lightImage=[&](ID2D1Bitmap* bitmap,float opacity) {
        target_->DrawBitmap(bitmap,expanded,opacity*worldOpacity_,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    };
    if(phase.glow>0) {
        const auto lines=[&] {
            lightImage(resource.halo.Get(),phase.glow*.85f*(visible?1.f:.4f));
            lightImage(resource.lines.Get(),phase.glow*(visible?1.f:.5f));
        };
        reveal(phase.lines,[&] {
            if(solid<constructionBands-1){push(resource.after[solid+1].Get(),1);lines();target_->PopLayer();}
            if(solid<constructionBands && partial<1){push(resource.bands[solid].Get(),1-partial);lines();target_->PopLayer();}
        });
        if(phase.material>0 && solid<constructionBands) {
            for(int i=std::max(0,solid-1);i<=std::min(constructionBands-1,solid+1);++i) {
                const float strength=std::max(0.f,1-std::abs((i+.5f)-materialLevel)/1.6f)*phase.glow;
                if(strength<=0)continue;
                push(resource.bands[i].Get(),strength);lightImage(resource.surface.Get(),visible?1.f:.5f);target_->PopLayer();
            }
        }
    }
}
}
