#include "RenderSupport.hpp"

namespace rts {
using namespace render;
ID2D1BitmapBrush* Renderer::terrainGridBrush(Cell ramp, float zoom) {
    // A periodic pair of line families in map coordinates, independent of map
    // dimensions, camera position and visibility. Retain only the current zoom.
    if (gridZoom_ != zoom) { terrainGrids_.clear(); gridZoom_ = zoom; }
    auto& brush = terrainGrids_[{ramp.x, ramp.y}];
    if (brush) return brush.Get();
    constexpr UINT resolution = 256;
    const WorldView view{{}, zoom};
    const auto u = view.project({1, 0}, float(ramp.x));
    const auto v = view.project({0, 1}, float(ramp.y));
    const float area = std::abs(u.x * v.y - u.y * v.x);
    const float spacingU = area / std::hypot(v.x, v.y), spacingV = area / std::hypot(u.x, u.y);
    std::array<float, resolution> coverageU{}, coverageV{};
    for (UINT i = 0; i < resolution; ++i) {
        const float p = (i + .5f) / resolution;
        const float distance = std::min(p, 1 - p);
        // One DIP stroke, filtered with a one-DIP pixel footprint. The ramp's
        // perpendicular spacing keeps both line families equally thin.
        coverageU[i] = std::max(0.f, 1 - distance * spacingU);
        coverageV[i] = std::max(0.f, 1 - distance * spacingV);
    }
    std::vector<uint32_t> pixels(resolution * resolution);
    for (UINT y = 0; y < resolution; ++y) for (UINT x = 0; x < resolution; ++x) {
        const auto alpha = uint32_t(std::lround(255 * .25f * std::max(coverageU[x], coverageV[y])));
        pixels[y * resolution + x] = (alpha << 24) | ((0xab * alpha / 255) << 16) |
            ((0xc4 * alpha / 255) << 8) | (0x94 * alpha / 255);
    }
    ComPtr<ID2D1Bitmap> bitmap;
    check(target_->CreateBitmap(D2D1::SizeU(resolution, resolution), pixels.data(), resolution * 4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96), bitmap.GetAddressOf()));
    check(target_->CreateBitmapBrush(bitmap.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR), brush.GetAddressOf()));
    brush->SetTransform(D2D1::Matrix3x2F::Scale(1.f / resolution, 1.f / resolution));
    return brush.Get();
}

void Renderer::terrainGrid(Cell cell, Cell ramp, const WorldView& view, const std::array<Vec2, 4>& top) {
    auto* brush = terrainGridBrush(ramp, view.zoom);
    brush->SetOpacity(worldOpacity_);
    D2D1_MATRIX_3X2_F previous;
    target_->GetTransform(&previous);
    target_->SetTransform(surfaceTransform(top, cell) * previous);
    const auto antialias = target_->GetAntialiasMode();
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    target_->FillRectangle(rect(float(cell.x), float(cell.y), 1, 1), brush);
    target_->SetAntialiasMode(antialias);
    target_->SetTransform(previous);
}
}
