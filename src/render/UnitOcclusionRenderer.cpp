#include "RenderSupport.hpp"
#include "rts/UnitOcclusion.hpp"

namespace rts {
using namespace render;
bool Renderer::beginUnitOcclusion(const Simulation& game, const WorldView& view, float depth, UiRect bounds) {
    std::vector<UnitOcclusion> windows;
    const auto extent = size();
    float left = extent.x, top = extent.y, right = 0, bottom = 0;
    for (const auto& unit : game.units()) {
        if (!occlusionEligible(game, unit, depth)) continue;
        const auto window = unitOcclusion(game, unit, view);
        const auto b = window.bounds();
        if (b.x >= bounds.x + bounds.width || b.y >= bounds.y + bounds.height ||
            b.x + b.width <= bounds.x || b.y + b.height <= bounds.y ||
            b.x > extent.x || b.y > extent.y || b.x + b.width < 0 || b.y + b.height < 0) continue;
        windows.push_back(window);
        left = std::min(left, b.x); top = std::min(top, b.y);
        right = std::max(right, b.x + b.width); bottom = std::max(bottom, b.y + b.height);
    }
    if (windows.empty()) return false;
    // Include an opaque border so the clamped brush leaves the rest of the object intact.
    left = std::floor(left) - 2; top = std::floor(top) - 2;
    const UINT width = UINT(std::ceil(right - left)) + 2, height = UINT(std::ceil(bottom - top)) + 2;
    std::vector<UINT32> pixels(size_t(width) * height, 0xffffffff);
    for (const auto& window : windows) {
        const auto b = window.bounds();
        const int x0 = std::max(0, int(std::floor(b.x - left))), y0 = std::max(0, int(std::floor(b.y - top)));
        const int x1 = std::min(int(width), int(std::ceil(b.x + b.width - left)));
        const int y1 = std::min(int(height), int(std::ceil(b.y + b.height - top)));
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
            const UINT32 alpha = UINT32(std::lround(255 * window.opacity({left + x + .5f, top + y + .5f})));
            auto& pixel = pixels[size_t(y) * width + x];
            pixel = std::min(pixel, alpha * 0x01010101u); // Union, never accumulate crowd transparency.
        }
    }
    ComPtr<ID2D1Bitmap> bitmap;
    ComPtr<ID2D1BitmapBrush> mask;
    check(target_->CreateBitmap(D2D1::SizeU(width, height), pixels.data(), width * 4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96), bitmap.GetAddressOf()));
    check(target_->CreateBitmapBrush(bitmap.Get(), D2D1::BitmapBrushProperties(), mask.GetAddressOf()));
    mask->SetTransform(D2D1::Matrix3x2F::Translation(left, top));
    if (!occlusionLayer_) check(target_->CreateLayer(nullptr, occlusionLayer_.GetAddressOf()));
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::Matrix3x2F::Identity(), 1, mask.Get()), occlusionLayer_.Get());
    return true;
}
}
