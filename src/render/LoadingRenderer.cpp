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

void Renderer::drawLoading(const Simulation& game, const std::wstring& stage, size_t completed, size_t total) {
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
    const LoadingBackdropLayout backdrop(extent);
    const auto draw = [&](size_t index, UiRect source, UiRect destination, float opacity = 1.f) {
        target_->DrawBitmap(loadingImages_[index].Get(), rect(destination.x, destination.y, destination.width, destination.height),
            opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, rect(source.x, source.y, source.width, source.height));
    };
    draw(0, {0, 0, 1672, 941}, {backdrop.origin.x, backdrop.origin.y, backdrop.size.x, backdrop.size.y});

    const LoadingLayout layout(extent);
    auto titleShadow = layout.title;
    titleShadow.y += 2;
    centeredText(wide(game.mapName()), titleShadow, 0x30291f, true);
    centeredText(wide(game.mapName()), layout.title, 0xf0deae, true);
    constexpr float resolution = float(MinimapRaster::resolution);
    sprite(loadingPreview_.Get(), rect(0, 0, resolution, resolution),
        {layout.map.x, layout.map.y}, {layout.map.width, layout.map.height});
    draw(1, {0, 0, 1254, 1254}, layout.frame);

    const float progress = total ? std::min(1.f, float(completed) / float(total)) : 0.f;
    draw(2, {0, 0, 2172, 248}, layout.bar);
    if (progress > 0) {
        target_->PushAxisAlignedClip(rect(layout.fill.x, layout.fill.y, layout.fill.width * progress, layout.fill.height),
            D2D1_ANTIALIAS_MODE_ALIASED);
        draw(2, {214, 250, 1745, 84}, layout.fill);
        target_->PopAxisAlignedClip();
    }
    // Center the digits alone; the suffix follows their measured advance.
    const auto digits = std::to_wstring(int(progress * 100));
    ComPtr<IDWriteTextLayout> number;
    check(writeFactory_->CreateTextLayout(digits.c_str(), static_cast<UINT32>(digits.size()), bodyFormat_.Get(),
        layout.fill.width, layout.fill.height, number.GetAddressOf()));
    check(number->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING));
    check(number->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
    DWRITE_TEXT_METRICS metrics{};
    check(number->GetMetrics(&metrics));
    const float numberX = layout.bar.x + (layout.bar.width - metrics.width) * .5f;
    brush_->SetColor(D2D1::ColorF(0xffedca));
    target_->DrawTextLayout(point({numberX, layout.fill.y}), number.Get(), brush_.Get());
    text(L"%", rect(numberX + metrics.width, layout.fill.y, layout.fill.width * .5f, layout.fill.height), 0xffedca);
    // Use the supplied lettering's alpha as gold ink, preserving the drawn typeface.
    const auto captionSource = rect(681, 374, 813, 123);
    const auto captionBounds = rect(layout.caption.x, layout.caption.y, layout.caption.width, layout.caption.height);
    const auto captionShadow = rect(layout.caption.x + 1, layout.caption.y + 2, layout.caption.width, layout.caption.height);
    const auto antialias = target_->GetAntialiasMode();
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    brush_->SetColor(D2D1::ColorF(0x17130e));
    target_->FillOpacityMask(loadingImages_[2].Get(), brush_.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, captionShadow, captionSource);
    brush_->SetColor(D2D1::ColorF(0xf2dba5));
    target_->FillOpacityMask(loadingImages_[2].Get(), brush_.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, captionBounds, captionSource);
    target_->SetAntialiasMode(antialias);
    draw(2, {320, 505, 1535, 191}, layout.ornament);
    auto statusShadow = layout.status;
    statusShadow.y += 1;
    centeredText(stage, statusShadow, 0x201c18);
    centeredText(stage, layout.status, 0xf0deae);
    const auto hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) discardTarget();
    else check(hr);
}
}
