#include "RenderSupport.hpp"
#include "rts/SpriteIsland.hpp"

namespace rts {
using namespace render;
ID2D1Bitmap* Renderer::worldBitmap(const WorldObjectDefinition& d) {
    if (!d.islandSeed) {
        auto& bitmap = worldSprites_[d.image];
        if (!bitmap) loadBitmap(paths_.asset(d.image), bitmap, 0, SpriteTeamMask::None, {}, true);
        return bitmap.Get();
    }
    auto& bitmap = worldIslands_[d.id];
    if (bitmap) return bitmap.Get();
    auto& sheet = worldSourcePixels_[d.image];
    if (sheet.bgra.empty()) {
        ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
        check(wic_->CreateDecoderFromFilename(paths_.asset(d.image).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
        check(decoder->GetFrame(0, frame.GetAddressOf()));
        check(wic_->CreateFormatConverter(converter.GetAddressOf()));
        check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        UINT width{}, height{}; check(converter->GetSize(&width, &height));
        if (!width || !height || width > 8192 || height > 8192) throw std::runtime_error("Invalid world sprite dimensions");
        sheet = {int(width), int(height), std::vector<std::uint8_t>(size_t(width) * height * 4)};
        check(converter->CopyPixels(nullptr, width * 4, UINT(sheet.bgra.size()), sheet.bgra.data()));
    }
    const auto pixels = extractSpriteIsland(sheet, {int(d.source[0]), int(d.source[1]), int(d.source[2]), int(d.source[3])}, *d.islandSeed);
    const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    check(target_->CreateBitmap(D2D1::SizeU(UINT(pixels.width), UINT(pixels.height)), pixels.bgra.data(), UINT(pixels.width * 4), properties, bitmap.GetAddressOf()));
    prepareSpriteLighting(bitmap.Get(), pixels.bgra, UINT(pixels.width), UINT(pixels.height), false);
    return bitmap.Get();
}
}
