#include "RenderSupport.hpp"
#include "rts/TeamColor.hpp"

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
        for (const auto& stage : e.buildingSprite.stages) {
            images.push_back(imagePath(stage.image));
            if (!stage.teamMask.empty()) images.push_back(imagePath(stage.teamMask));
            for (const auto& layer : stage.layers) {
                images.push_back(imagePath(layer.image));
                if (!layer.teamMask.empty()) images.push_back(imagePath(layer.teamMask));
            }
        }
    }
    for (const auto& commander : definitions.commanders()) if (commander.rallySprite) {
        images.push_back(imagePath(commander.rallySprite->image));
        if (!commander.rallySprite->teamMask.empty()) images.push_back(imagePath(commander.rallySprite->teamMask));
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

void Renderer::loadBitmap(const std::filesystem::path& path, ComPtr<ID2D1Bitmap>& bitmap, unsigned teamMask,
    SpriteTeamMask palette, const std::filesystem::path& maskPath) {
    ComPtr<IWICBitmapDecoder> decoder;
    check(wic_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.GetAddressOf()));
    ComPtr<IWICFormatConverter> converter;
    check(wic_->CreateFormatConverter(converter.GetAddressOf()));
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (palette != SpriteTeamMask::None || !maskPath.empty()) {
        UINT width{}, height{};
        check(converter->GetSize(&width, &height));
        if (!width || !height || width > 8192 || height > 8192) throw std::runtime_error("Invalid sprite dimensions");
        std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
        check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
        if (!maskPath.empty()) {
            ComPtr<IWICBitmapDecoder> maskDecoder; ComPtr<IWICBitmapFrameDecode> maskFrame; ComPtr<IWICFormatConverter> maskConverter;
            check(wic_->CreateDecoderFromFilename(maskPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, maskDecoder.GetAddressOf()));
            check(maskDecoder->GetFrame(0, maskFrame.GetAddressOf()));
            UINT maskWidth{}, maskHeight{}; check(maskFrame->GetSize(&maskWidth, &maskHeight));
            if (maskWidth != width || maskHeight != height) throw std::runtime_error("Team mask dimensions must match the original PNG");
            check(wic_->CreateFormatConverter(maskConverter.GetAddressOf()));
            check(maskConverter->Initialize(maskFrame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
            std::vector<BYTE> mask(pixels.size());
            check(maskConverter->CopyPixels(nullptr, width * 4, static_cast<UINT>(mask.size()), mask.data()));
            applyTeamColorMask(pixels, mask, teamMask);
        } else for (size_t i = 0; i < pixels.size(); i += 4) {
            const int blue = pixels[i], green = pixels[i + 1], red = pixels[i + 2];
            // Palette accents in the placeholder sheet, preserving skin and neutral armour.
            if ((palette == SpriteTeamMask::Blue && blue > red + 10 && green >= red && blue >= green - 10) ||
                (palette == SpriteTeamMask::Purple && red > green + 15 && blue > green + 15)) {
                if (teamMask == 0 || teamMask == 0xffffff) {
                    const unsigned alpha = pixels[i + 3];
                    const double luminosity = alpha ? std::clamp((.11 * blue + .59 * green + .30 * red) / alpha, 0.0, 1.0) : 0;
                    const auto shade = static_cast<BYTE>(std::lround(neutralTeamLuminosity(luminosity, teamMask == 0xffffff) * alpha));
                    pixels[i] = pixels[i + 1] = pixels[i + 2] = shade;
                } else {
                    const int intensity = std::max(red, blue);
                    pixels[i] = static_cast<BYTE>((teamMask & 255) * intensity / 255);
                    pixels[i + 1] = static_cast<BYTE>(((teamMask >> 8) & 255) * intensity / 255);
                    pixels[i + 2] = static_cast<BYTE>(((teamMask >> 16) & 255) * intensity / 255);
                }
            }
        }
        check(target_->CreateBitmap(D2D1::SizeU(width, height), pixels.data(), width * 4, properties, bitmap.ReleaseAndGetAddressOf()));
    } else check(target_->CreateBitmapFromWicBitmap(converter.Get(), properties, bitmap.ReleaseAndGetAddressOf()));
}

ID2D1Bitmap* Renderer::maskedBitmap(const std::string& image, const std::string& teamMask, unsigned color) {
    const auto path = imagePath(image), mask = imagePath(teamMask);
    const auto tint = mask.empty() ? 0 : color;
    auto& bitmap = maskedImages_[{path, mask, tint}];
    if (!bitmap) loadBitmap(paths_.asset(path), bitmap, tint, SpriteTeamMask::None,
        mask.empty() ? std::filesystem::path{} : paths_.asset(mask));
    return bitmap.Get();
}

void Renderer::loadResources() {
    check(target_->CreateSolidColorBrush(D2D1::ColorF(0xffffff), brush_.GetAddressOf()));
    groundBrush_ = materialResource(worldAssets_.materials().front().id).brush;
    loadBitmap(paths_.asset(L"sprites/hall.png"), hall_);
    loadBitmap(paths_.asset(L"sprites/crystal.png"), crystal_);
    loadBitmap(paths_.asset(L"sprites/worker.png"), worker_, teamColor_, SpriteTeamMask::Blue);
    loadBitmap(paths_.asset(L"sprites/worker.png"), enemy_, enemyColor_, SpriteTeamMask::Blue);
    loadBitmap(paths_.asset(L"sprites/tree.png"), tree_);
    loadBitmap(paths_.asset(L"ui/menu-background.png"), menuBackground_);
    loadBitmap(paths_.asset(L"ui/logo.png"), logo_);
    // Optional artwork: command buttons and unit cards also work without a frame.
    if (std::filesystem::exists(paths_.assetRoot() / buttonFrameAsset))
        loadBitmap(paths_.asset(buttonFrameAsset), buttonFrame_);
}
}
