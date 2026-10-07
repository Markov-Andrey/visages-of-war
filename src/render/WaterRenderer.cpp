#include "RenderSupport.hpp"
#include <algorithm>
#include <cmath>

namespace rts {
using namespace render;

void Renderer::waterTile(const Map& map, Cell c, const WorldView& view) {
    // Cache by the local depth pattern, not map identity: edits and map switches
    // select the right variant immediately. A one-pixel gutter filters cell seams.
    unsigned key = 0, shift = 0;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        const Cell sample{std::clamp(c.x + x, 0, map.width() - 1), std::clamp(c.y + y, 0, map.height() - 1)};
        const auto& t = map.at(sample);
        const unsigned kind = t.height != map.at(c).height ? 3u : t.surface == Surface::Land ? 0u :
            t.surface == Surface::ShallowWater ? 1u : 2u;
        key |= kind << shift;
        shift += 2;
    }
    auto& waterBrush = waterTiles_[key];
    constexpr int resolution = 32, extent = resolution + 2;
    if (!waterBrush) {
        std::array<uint32_t, extent * extent> pixels{};
        for (int y = 0; y < extent; ++y) for (int x = 0; x < extent; ++x) {
            const Vec2 position{c.x + (x - .5f) / resolution, c.y + (y - .5f) / resolution};
            const float depth = map.waterDepth(c, position);
            const float deep = std::clamp((depth - .8f), 0.0f, 1.0f);
            const float alpha = std::lerp(.46f, .97f, deep) * std::min(1.0f, depth / .8f);
            // Premultiplied turquoise shallows over the authored ground/paint;
            // the deeper column gradually hides the same bed texture.
            const unsigned a = unsigned(alpha * 255 + .5f);
            unsigned pixel = a << 24;
            for (int channel = 0; channel < 3; ++channel) {
                constexpr unsigned shallow = 0x529e9b, deepColor = 0x204b64;
                const int bits = channel * 8;
                const float color = std::lerp(float((shallow >> bits) & 255), float((deepColor >> bits) & 255), deep);
                pixel |= unsigned(color * alpha + .5f) << bits;
            }
            pixels[size_t(y) * extent + x] = pixel;
        }
        const auto properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        ComPtr<ID2D1Bitmap> bitmap;
        check(target_->CreateBitmap(D2D1::SizeU(extent, extent), pixels.data(), extent * 4, properties, bitmap.GetAddressOf()));
        check(target_->CreateBitmapBrush(bitmap.Get(), D2D1::BitmapBrushProperties(
            D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR), waterBrush.GetAddressOf()));
    }
    const auto top = map.surfaceCorners(c, view);
    D2D1_MATRIX_3X2_F previous;
    target_->GetTransform(&previous);
    target_->SetTransform(surfaceTransform(top, c) * previous);
    // Aliased coverage, filtered texture: DrawBitmap antialiases each destination
    // independently and exposes the bright bed along fractional pixel boundaries.
    waterBrush->SetTransform(D2D1::Matrix3x2F::Scale(1.0f / resolution, 1.0f / resolution) *
        D2D1::Matrix3x2F::Translation(c.x - 1.0f / resolution, c.y - 1.0f / resolution));
    waterBrush->SetOpacity(worldOpacity_);
    const auto antialias = target_->GetAntialiasMode();
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    target_->FillRectangle(rect(float(c.x), float(c.y), 1, 1), waterBrush.Get());
    target_->SetAntialiasMode(antialias);
    target_->SetTransform(previous);

    // Sparse, low-contrast ripples animate in simulation time (pause also pauses water).
    const float previousOpacity = worldOpacity_;
    for (int i = 0; i < 2; ++i) {
        const float phase = c.x * 2.37f + c.y * 1.71f + i * 2.6f + waterSeconds_ * 1.15f;
        const float drift = std::sin(phase) * .025f;
        const float offset = float((c.x * 7 + c.y * 11 + i * 3) % 9) * .035f;
        const Vec2 a{c.x + .13f + offset, c.y + .28f + i * .43f + drift};
        const Vec2 b{a.x + .17f + .04f * std::sin(phase + 1), a.y};
        worldOpacity_ = previousOpacity * (.09f + .07f * std::sin(phase));
        line(view.project(a, map.surfaceHeight(c, a)), view.project(b, map.surfaceHeight(c, b)),
            0xc2e0d2, std::max(.6f, view.zoom));
    }
    worldOpacity_ = previousOpacity;
}

void Renderer::wadingUnitImage(const UnitSpriteDefinition& definition, int column, int row,
    Vec2 ground, Vec2 waterline, float immersion, float zoom, unsigned color) {
    if (immersion <= .001f) {
        unitImage(definition, column, row, ground, zoom, color);
        return;
    }
    const float previousOpacity = worldOpacity_;
    const float width = definition.size.x * zoom + 4;
    const float height = definition.size.y * zoom + 4;
    // Only the submerged part is attenuated; the body retains crisp classic frames.
    target_->PushAxisAlignedClip(rect(ground.x - width, waterline.y, width * 2, height), D2D1_ANTIALIAS_MODE_ALIASED);
    worldOpacity_ *= .22f;
    unitImage(definition, column, row, ground, zoom, color);
    target_->PopAxisAlignedClip();
    worldOpacity_ = previousOpacity;
    target_->PushAxisAlignedClip(rect(ground.x - width, ground.y - height, width * 2,
        std::max(0.0f, waterline.y - ground.y + height)), D2D1_ANTIALIAS_MODE_ALIASED);
    unitImage(definition, column, row, ground, zoom, color);
    target_->PopAxisAlignedClip();
    const float pulse = .9f + .1f * std::sin(waterSeconds_ * 3 + ground.x * .02f);
    brush_->SetColor(D2D1::ColorF(0xaad5cf, previousOpacity * std::min(1.0f, immersion / .8f) * .35f));
    target_->DrawEllipse(D2D1::Ellipse(point(waterline), 12 * zoom * pulse, 3 * zoom * pulse), brush_.Get(), std::max(.7f, zoom));
}
}
