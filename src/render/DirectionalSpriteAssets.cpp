#include "DirectionalSpriteAssets.hpp"
#include "RenderSupport.hpp"
#include <map>

namespace rts::render {
template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
DirectionalSpriteRecipe directionalRecipe(const Paths& paths, const UnitSpriteDefinition& sprite) {
    auto recipe = loadDirectionalSpriteRecipe(paths, sprite.directionRecipe);
    if (recipe.width != sprite.frameWidth || recipe.height != sprite.frameHeight)
        throw std::runtime_error("Directional recipe canvas disagrees with unit sprite");
    // The recipe pivot aligns source frames during synthesis. The sprite anchor
    // independently places the finished artwork relative to the selection ring.
    return recipe;
}
SpritePixels loadDirectionalSprite(const Paths& paths, IWICImagingFactory* wic, const UnitSpriteDefinition& sprite, unsigned color) {
    const auto recipe = directionalRecipe(paths, sprite);
    std::map<std::pair<std::string, std::string>, ComPtr<IWICBitmapSource>> images;
    std::array<std::array<SpritePixels, 9>, 2> frames;
    for (size_t direction = 0; direction < 2; ++direction) for (size_t column = 0; column < 9; ++column) {
        const auto& frame = recipe.sources[direction][column];
        auto& image = images[{frame.image, frame.teamMask}];
        if (!image) {
            ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> decoded; ComPtr<IWICFormatConverter> converted;
            const auto path = paths.asset(imagePath(frame.image));
            check(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
            check(decoder->GetFrame(0, decoded.GetAddressOf()));
            check(wic->CreateFormatConverter(converted.GetAddressOf()));
            check(converted->Initialize(decoded.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
            ComPtr<IWICBitmap> cached;
            check(wic->CreateBitmapFromSource(converted.Get(), WICBitmapCacheOnLoad, cached.GetAddressOf()));
            if (!frame.teamMask.empty()) {
                UINT width{}, height{}; check(cached->GetSize(&width, &height));
                if (!width || !height || width > 8192 || height > 8192)
                    throw std::runtime_error("Invalid directional source dimensions");
                std::vector<BYTE> pixels(size_t(width) * height * 4);
                check(cached->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data()));
                applyImageTeamMask(wic, paths.asset(imagePath(frame.teamMask)), width, height, pixels, color);
                check(wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA, width * 4,
                    UINT(pixels.size()), pixels.data(), cached.ReleaseAndGetAddressOf()));
            }
            image = cached;
        }
        UINT width{}, height{}; check(image->GetSize(&width, &height));
        const auto& r = frame.source;
        if (!width || !height || width > 8192 || height > 8192 || r[0] + r[2] > int(width) || r[1] + r[3] > int(height))
            throw std::runtime_error("Directional source outside image: " + frame.image);
        const WICRect crop{r[0], r[1], r[2], r[3]};
        ComPtr<IWICBitmapClipper> clipper; ComPtr<IWICBitmapScaler> scaler;
        check(wic->CreateBitmapClipper(clipper.GetAddressOf()));
        check(clipper->Initialize(image.Get(), &crop));
        check(wic->CreateBitmapScaler(scaler.GetAddressOf()));
        const int scaledWidth = std::max(1, int(std::lround(r[2] * frame.scale)));
        const int scaledHeight = std::max(1, int(std::lround(r[3] * frame.scale)));
        if (scaledWidth > recipe.width || scaledHeight > recipe.height)
            throw std::runtime_error("Directional source cannot fit its canvas");
        check(scaler->Initialize(clipper.Get(), UINT(scaledWidth), UINT(scaledHeight), WICBitmapInterpolationModeFant));
        std::vector<std::uint8_t> pixels(size_t(scaledWidth) * scaledHeight * 4);
        check(scaler->CopyPixels(nullptr, UINT(scaledWidth * 4), UINT(pixels.size()), pixels.data()));
        // Fixed, authored foot pivots avoid animation wobble from weapon-dependent bounding boxes.
        const int left = int(std::lround(recipe.pivot.x - (frame.pivot.x - r[0]) * scaledWidth / r[2]));
        const int top = int(std::lround(recipe.pivot.y - (frame.pivot.y - r[1]) * scaledHeight / r[3]));
        if (left < 0 || top < 0 || left + scaledWidth > recipe.width || top + scaledHeight > recipe.height)
            throw std::runtime_error("Directional frame is clipped by its canvas: " + frame.image);
        auto& output = frames[direction][column];
        output = {recipe.width, recipe.height, std::vector<std::uint8_t>(size_t(recipe.width) * recipe.height * 4)};
        for (int y = 0; y < scaledHeight; ++y)
            std::copy_n(pixels.data() + size_t(y) * scaledWidth * 4, size_t(scaledWidth) * 4,
                output.bgra.data() + (size_t(y + top) * recipe.width + left) * 4);
    }
    return assembleDirectionalSprite(recipe, frames);
}
}
