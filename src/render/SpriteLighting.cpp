#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::prepareSpriteLighting(ID2D1Bitmap* bitmap, std::span<const BYTE> pixels, UINT width, UINT height, bool highlights) {
    auto& resource = spriteLights_[bitmap];
    const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    std::vector<BYTE> opaque(pixels.begin(), pixels.end());
    std::vector<std::uint32_t> mask(highlights ? size_t(width) * height : 0);
    const auto* input = pixels.data();
    auto* output = opaque.data();
    for (size_t i = 0; i < opaque.size(); i += 4) {
        const unsigned alpha = input[i + 3];
        // Most art is either opaque or empty; only antialiased edges need division.
        if (alpha && alpha < 255) for (size_t channel = 0; channel < 3; ++channel)
            output[i + channel] = static_cast<BYTE>(std::min(255u, (input[i + channel] * 255u + alpha / 2) / alpha));
        output[i + 3] = 255;
        if (highlights) {
            const auto pixel = std::uint32_t(input[i]) | (std::uint32_t(input[i + 1]) << 8) |
                (std::uint32_t(input[i + 2]) << 16) | (std::uint32_t(alpha) << 24);
            mask[i / 4] = std::uint32_t(highlightEmission(pixel)) * 0x01010101u;
        }
    }
    // Extend edge colours into transparent texels for linear filtering of straight RGB.
    // Original alpha is kept in the opacity brush and applied exactly once on compositing.
    for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
        const size_t i = (size_t(y) * width + x) * 4;
        if (input[i + 3]) continue;
        unsigned total = 0, blue = 0, green = 0, red = 0;
        const UINT left = x ? x - 1 : 0, right = std::min(x + 1, width - 1);
        const UINT top = y ? y - 1 : 0, bottom = std::min(y + 1, height - 1);
        for (UINT sy = top; sy <= bottom; ++sy) for (UINT sx = left; sx <= right; ++sx) {
            const auto* neighbor = input + (size_t(sy) * width + sx) * 4;
            if (!neighbor[3]) continue;
            total += neighbor[3];
            blue += neighbor[0]; green += neighbor[1]; red += neighbor[2];
        }
        if (total) {
            output[i] = static_cast<BYTE>(std::min(255u, blue * 255 / total));
            output[i + 1] = static_cast<BYTE>(std::min(255u, green * 255 / total));
            output[i + 2] = static_cast<BYTE>(std::min(255u, red * 255 / total));
        }
    }
    check(target_->CreateBitmap(D2D1::SizeU(width, height), opaque.data(), width * 4, properties, resource.opaque.GetAddressOf()));
    check(target_->CreateBitmapBrush(bitmap, resource.alphaBrush.GetAddressOf()));
    check(target_->CreateBitmapBrush(resource.opaque.Get(), resource.colorBrush.GetAddressOf()));
    if (highlights) check(target_->CreateBitmap(D2D1::SizeU(width, height), mask.data(), width * 4, properties, resource.highlights.GetAddressOf()));
}

ID2D1Bitmap* Renderer::emissionBitmap(const std::string& image) {
    auto& bitmap = emissionMasks_[image];
    if (bitmap) return bitmap.Get();
    ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
    const auto path = paths_.asset(imagePath(image));
    check(wic_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
    check(decoder->GetFrame(0, frame.GetAddressOf()));
    check(wic_->CreateFormatConverter(converter.GetAddressOf()));
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    UINT w{}, h{}; check(converter->GetSize(&w, &h));
    if (!w || !h || w > 8192 || h > 8192) throw std::runtime_error("Invalid emission mask dimensions");
    std::vector<std::uint32_t> pixels(size_t(w) * h);
    check(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size() * 4), reinterpret_cast<BYTE*>(pixels.data())));
    for (auto& pixel : pixels) pixel = std::uint32_t(emissionCoverage(pixel)) * 0x01010101u;
    const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    check(target_->CreateBitmap(D2D1::SizeU(w, h), pixels.data(), w * 4, properties, bitmap.GetAddressOf()));
    return bitmap.Get();
}

void Renderer::litSprite(ID2D1Bitmap* bitmap, D2D1_RECT_F source, Vec2 topLeft, Vec2 extent, bool pixel, float response, ID2D1Bitmap* emission) {
    auto& resource = spriteLights_.at(bitmap);
    const auto destination = rect(topLeft.x, topLeft.y, extent.x, extent.y);
    const auto interpolation = pixel ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    const auto mapping = D2D1::Matrix3x2F::Translation(-source.left, -source.top) *
        D2D1::Matrix3x2F::Scale(extent.x / (source.right - source.left), extent.y / (source.bottom - source.top)) *
        D2D1::Matrix3x2F::Translation(topLeft.x, topLeft.y);
    resource.alphaBrush->SetTransform(mapping);
    resource.alphaBrush->SetInterpolationMode(interpolation);
    D2D1::Matrix3x2F lightMapping, inverse;
    nightBrush_->GetTransform(&lightMapping);
    target_->GetTransform(&inverse);
    inverse.Invert();
    nightBrush_->SetTransform(lightMapping * inverse);
    nightBrush_->SetOpacity(response);
    // Compose opaque colour, surface lighting and emission inside the original silhouette.
    // Later scenery (including the dome) still occludes the whole result in normal depth order.
    target_->PushLayer(D2D1::LayerParameters(destination, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), worldOpacity_, resource.alphaBrush.Get()), spriteLightLayer_.Get());
    target_->DrawBitmap(resource.opaque.Get(), destination, 1, interpolation, source);
    target_->FillRectangle(destination, nightBrush_.Get());
    if (emission) {
        const auto antialias = target_->GetAntialiasMode();
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        resource.colorBrush->SetTransform(mapping);
        resource.colorBrush->SetInterpolationMode(interpolation);
        target_->FillOpacityMask(emission, resource.colorBrush.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, destination, source);
        target_->SetAntialiasMode(antialias);
    }
    target_->PopLayer();
    nightBrush_->SetTransform(lightMapping);
}
}
