#include "RenderSupport.hpp"

namespace rts {
using namespace render;
namespace {
float smoothFade(float value) {
    const float t = std::clamp(value, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
}

void Renderer::drawSelectionRing(Vec2 center, int radiusX, int radiusY, float zoom, unsigned color, float opacity, bool dashed) {
    if (radiusX <= 0 || radiusY <= 0 || zoom <= 0) return;
    // Generated once per shape/colour, at four samples per world-render pixel.
    // Camera zoom only scales the cached image; no external ring artwork is used.
    constexpr int samples = 4, padding = 8;
    const int width = (radiusX + padding) * 2, height = (radiusY + padding) * 2;
    auto& bitmap = selectionRings_[{color, radiusX, radiusY, dashed}];
    if (!bitmap) {
        const int pixelWidth = width * samples, pixelHeight = height * samples;
        std::vector<BYTE> pixels(size_t(pixelWidth) * pixelHeight * 4);
        const float rx = float(radiusX), ry = float(radiusY);
        const std::array<float, 3> tint{{float(color & 255) / 255.f,
            float((color >> 8) & 255) / 255.f, float((color >> 16) & 255) / 255.f}};
        for (int y = 0; y < pixelHeight; ++y) for (int x = 0; x < pixelWidth; ++x) {
            const float nx = ((x + .5f) / samples - width * .5f) / rx;
            const float ny = ((y + .5f) / samples - height * .5f) / ry;
            const float radius = std::sqrt(nx * nx + ny * ny);
            if (radius < .001f) continue;
            // First-order signed distance to the ellipse, in render pixels.
            // This keeps the rim equally thin at its sides and at its front.
            const float gradient = std::sqrt(nx * nx / (rx * rx) + ny * ny / (ry * ry)) / radius;
            const float distance = (radius - 1.f) / gradient;
            // Fade both rear arms smoothly to nothing before they meet at the top.
            float rear = smoothFade((ny / radius + .96f) / .74f);
            if (dashed) {
                const float phase = (std::atan2(ny, nx) + 3.141592654f) * (20.f / 6.283185307f);
                const float segment = phase - std::floor(phase);
                rear *= smoothFade(segment / .06f) * smoothFade((.70f - segment) / .06f);
            }
            if (rear == 0.f || std::abs(distance) >= 6.f) continue;

            const float halo = .56f * smoothFade(1.f - std::abs(distance) / 6.f);
            const float lip = .70f * std::exp(-std::pow((distance - .85f) / .65f, 2.f));
            const float rim = .98f * std::exp(-std::pow((distance + .20f) / .65f, 2.f));
            // A darker outer lip separates the luminous inner edge from the halo.
            // Compose in premultiplied BGRA so transparent edges never get black fringes.
            const float underAlpha = lip + halo * (1.f - lip);
            const float alpha = (rim + underAlpha * (1.f - rim)) * rear;
            const size_t offset = (size_t(y) * pixelWidth + x) * 4;
            for (size_t channel = 0; channel < tint.size(); ++channel) {
                const float under = tint[channel] * (.24f * lip + .86f * halo * (1.f - lip));
                const float highlight = tint[channel] + (1.f - tint[channel]) * .24f;
                const float value = (highlight * rim + under * (1.f - rim)) * rear;
                pixels[offset + channel] = static_cast<BYTE>(std::lround(std::clamp(value, 0.f, alpha) * 255.f));
            }
            pixels[offset + 3] = static_cast<BYTE>(std::lround(alpha * 255.f));
        }
        const auto properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        check(target_->CreateBitmap(D2D1::SizeU(pixelWidth, pixelHeight), pixels.data(), pixelWidth * 4,
            properties, bitmap.ReleaseAndGetAddressOf()));
    }
    const Vec2 extent{width * zoom, height * zoom};
    const auto source = bitmap->GetSize();
    // Selection stays readable at night but retains scene occlusion and fog visibility.
    target_->DrawBitmap(bitmap.Get(), rect(center.x - extent.x * .5f, center.y - extent.y * .5f, extent.x, extent.y),
        worldOpacity_ * std::clamp(opacity, 0.f, 1.f), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, rect(0, 0, source.width, source.height));
}
void Renderer::drawCommandPulse(Vec2 center, int radiusX, int radiusY, float zoom, unsigned color, float age) {
    if (age < 0) return;
    for (int pulse = 0; pulse < 2; ++pulse) {
        const float phase = (age - pulse * CommandFeedback::pulseDelay) / CommandFeedback::pulseSeconds;
        if (phase < 0 || phase >= 1) continue;
        // Scale a cached outer ring; animation never creates per-frame bitmaps.
        drawSelectionRing(center, radiusX + 5, radiusY + 3, zoom * (1 + .22f * phase), color,
            .85f * (1 - phase), true);
    }
}
}
