#include "RenderSupport.hpp"
#include "rts/LoadingScreen.hpp"

namespace rts {
using namespace render;
void Renderer::beginLoadingPreview(const Simulation& game) {
    // Independent preview fog: viewing the loading screen must never reveal the match.
    FogOfWar fog(game.map().width(), game.map().height());
    fog.revealAll();
    FogMask mask;
    mask.update(fog, game.map().width(), game.map().height());
    std::vector<const EnvironmentObject*> obstacles;
    for (const auto& object : game.environment()) if (object.active()) obstacles.push_back(&object);
    MinimapRaster raster;
    raster.update(game.map(), fog, mask, obstacles);
    loadingPreviewPixels_ = raster.pixels();
    loadingPreview_.Reset();
}

void Renderer::drawLoading(const Simulation& game, const std::wstring& stage, size_t completed, size_t total, Vec2 parallax) {
    ensureTarget();
    worldOpacity_ = 1;
    nightActive_ = false;
    for (size_t i = 0; i < loadingImages_.size(); ++i)
        if (!loadingImages_[i]) loadBitmap(paths_.asset(loadingImagePaths[i]), loadingImages_[i]);
    if (loadingPreviewPixels_.empty()) beginLoadingPreview(game);
    if (!loadingPreview_) {
        constexpr auto resolution = MinimapRaster::resolution;
        check(target_->CreateBitmap(D2D1::SizeU(resolution, resolution), loadingPreviewPixels_.data(), resolution * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96),
            loadingPreview_.GetAddressOf()));
    }
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0xc6b28a));
    const auto extent = size();
    const LoadingBackdropLayout backdrop(extent, parallax);
    const auto draw = [&](size_t index, UiRect source, UiRect destination, float opacity = 1.f) {
        target_->DrawBitmap(loadingImages_[index].Get(), rect(destination.x, destination.y, destination.width, destination.height),
            opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, rect(source.x, source.y, source.width, source.height));
    };
    const auto layer = [&](size_t index, float depth) {
        const auto pixels = loadingImages_[index]->GetSize();
        draw(index, {0, 0, pixels.width, pixels.height}, backdrop.layer({0, 0, 1672, 941}, depth));
    };
    layer(0, .15f);
    draw(1, {0, 0, 1140, 728}, backdrop.layer({200, 14, 410, 262}, .35f), .62f);
    draw(1, {1440, 60, 615, 630}, backdrop.layer({1350, 24, 245, 251}, .35f), .64f);
    layer(2, .5f);
    layer(3, .7f);
    layer(4, 1.f);

    const LoadingLayout layout(extent);
    auto titleShadow = layout.title;
    titleShadow.y += 2;
    centeredText(wide(game.mapName()), titleShadow, 0x30291f, true);
    centeredText(wide(game.mapName()), layout.title, 0xf0deae, true);
    constexpr float resolution = float(MinimapRaster::resolution);
    sprite(loadingPreview_.Get(), rect(0, 0, resolution, resolution),
        {layout.map.x, layout.map.y}, {layout.map.width, layout.map.height});
    draw(5, {0, 0, 1254, 1254}, layout.frame);

    const float progress = total ? std::min(1.f, float(completed) / float(total)) : 0.f;
    draw(6, {0, 0, 2172, 248}, layout.bar);
    if (progress > 0) {
        target_->PushAxisAlignedClip(rect(layout.fill.x, layout.fill.y, layout.fill.width * progress, layout.fill.height),
            D2D1_ANTIALIAS_MODE_ALIASED);
        draw(6, {214, 250, 1745, 84}, layout.fill);
        target_->PopAxisAlignedClip();
    }
    centeredText(std::to_wstring(int(progress * 100)) + L"%", layout.fill, 0xffedca);
    // Use the supplied lettering's alpha as gold ink, preserving the drawn typeface.
    const auto captionSource = rect(681, 374, 813, 123);
    const auto captionBounds = rect(layout.caption.x, layout.caption.y, layout.caption.width, layout.caption.height);
    const auto captionShadow = rect(layout.caption.x + 1, layout.caption.y + 2, layout.caption.width, layout.caption.height);
    const auto antialias = target_->GetAntialiasMode();
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    brush_->SetColor(D2D1::ColorF(0x17130e));
    target_->FillOpacityMask(loadingImages_[6].Get(), brush_.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, captionShadow, captionSource);
    brush_->SetColor(D2D1::ColorF(0xf2dba5));
    target_->FillOpacityMask(loadingImages_[6].Get(), brush_.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, captionBounds, captionSource);
    target_->SetAntialiasMode(antialias);
    draw(6, {320, 505, 1535, 191}, layout.ornament);
    auto statusShadow = layout.status;
    statusShadow.y += 1;
    centeredText(stage, statusShadow, 0x201c18);
    centeredText(stage, layout.status, 0xf0deae);
    const auto hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) discardTarget();
    else check(hr);
}
}
