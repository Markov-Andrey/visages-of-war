#include "RenderSupport.hpp"

namespace { constexpr auto buttonFrameAsset = L"ui/button-frame.png"; }

namespace rts {
using namespace render;
void Renderer::verifyAssets() {
    validateWorldAssets(worldAssets_);
    const auto definitions = Definitions::load(paths_.asset(L"data/catalog.json"));
    validateCombatAssets(definitions);
    // Decode all files, including on --verify-assets, without creating a window.
    std::vector<std::filesystem::path> images{L"sprites/hall.png", L"sprites/crystal.png", L"sprites/worker.png", L"sprites/tree.png",
        L"ui/menu-background.png", L"ui/logo.png", L"ui/project-icon.png"};
    if (std::filesystem::exists(paths_.assetRoot() / buttonFrameAsset)) images.emplace_back(buttonFrameAsset);
    for (const auto& e : definitions.entities()) {
        if (e.mobile) images.push_back(imagePath(e.sprite.image));
        if (e.projectile) images.push_back(imagePath(e.projectile->image));
    }
    for (const auto& material : worldAssets_.materials()) images.push_back(material.image);
    for (const auto& object : worldAssets_.objects()) if (!object.image.empty()) images.push_back(object.image);
    for (const auto& name : images) {
        ComPtr<IWICBitmapDecoder> decoder;
        const auto path = paths_.asset(name);
        check(wic_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
        ComPtr<IWICBitmapFrameDecode> frame;
        check(decoder->GetFrame(0, frame.GetAddressOf()));
        ComPtr<IWICFormatConverter> converter;
        check(wic_->CreateFormatConverter(converter.GetAddressOf()));
        check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        UINT width{}, height{};
        check(converter->GetSize(&width, &height));
        if (!width || !height || width > 8192 || height > 8192) throw std::runtime_error("Invalid asset dimensions");
        std::vector<BYTE> data(static_cast<size_t>(width) * height * 4);
        check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(data.size()), data.data()));
    }
}

void Renderer::loadBitmap(const std::filesystem::path& path, ComPtr<ID2D1Bitmap>& bitmap, unsigned teamMask, SpriteTeamMask palette) {
    ComPtr<IWICBitmapDecoder> decoder;
    check(wic_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.GetAddressOf()));
    ComPtr<IWICFormatConverter> converter;
    check(wic_->CreateFormatConverter(converter.GetAddressOf()));
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (teamMask) {
        UINT width{}, height{};
        check(converter->GetSize(&width, &height));
        std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
        check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
        for (size_t i = 0; i < pixels.size(); i += 4) {
            const int blue = pixels[i], green = pixels[i + 1], red = pixels[i + 2];
            // Palette accents in the placeholder sheet, preserving skin and neutral armour.
            if ((palette == SpriteTeamMask::Blue && blue > red + 10 && green >= red && blue >= green - 10) ||
                (palette == SpriteTeamMask::Purple && red > green + 15 && blue > green + 15)) {
                const int intensity = std::max(red, blue);
                pixels[i] = static_cast<BYTE>((teamMask & 255) * intensity / 255);
                pixels[i + 1] = static_cast<BYTE>(((teamMask >> 8) & 255) * intensity / 255);
                pixels[i + 2] = static_cast<BYTE>(((teamMask >> 16) & 255) * intensity / 255);
            }
        }
        check(target_->CreateBitmap(D2D1::SizeU(width, height), pixels.data(), width * 4, properties, bitmap.ReleaseAndGetAddressOf()));
    } else check(target_->CreateBitmapFromWicBitmap(converter.Get(), properties, bitmap.ReleaseAndGetAddressOf()));
}

void Renderer::loadResources() {
    check(target_->CreateSolidColorBrush(D2D1::ColorF(0xffffff), brush_.GetAddressOf()));
    groundBrush_ = materialResource(worldAssets_.materials().front().id).brush;
    loadBitmap(paths_.asset(L"sprites/hall.png"), hall_);
    loadBitmap(paths_.asset(L"sprites/crystal.png"), crystal_);
    loadBitmap(paths_.asset(L"sprites/worker.png"), worker_, teamColor_);
    loadBitmap(paths_.asset(L"sprites/worker.png"), enemy_, enemyColor_);
    loadBitmap(paths_.asset(L"sprites/tree.png"), tree_);
    loadBitmap(paths_.asset(L"ui/menu-background.png"), menuBackground_);
    loadBitmap(paths_.asset(L"ui/logo.png"), logo_);
    // Optional artwork: command buttons and unit cards also work without a frame.
    if (std::filesystem::exists(paths_.assetRoot() / buttonFrameAsset))
        loadBitmap(paths_.asset(buttonFrameAsset), buttonFrame_);
}
}
