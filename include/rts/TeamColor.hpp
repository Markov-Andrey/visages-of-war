#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace rts {
// Neutral team paints: Screen/Multiply of the original luminosity with itself.
// This is an intentional game-specific extension to Color blending. Endpoints
// stay at 0 and 1, while the monotone curves retain the order of shading details.
inline double neutralTeamLuminosity(double luminosity, bool white) {
    return white ? luminosity * (2 - luminosity) : luminosity * luminosity;
}
// Warm paints need brighter midtones to read as orange/gold/yellow on dark art.
// Smooth hue ramps avoid a special case for each palette entry; low-saturation
// paints receive less lift. Red, green, cool colours and neutrals are unchanged.
inline double warmTeamHighlightStrength(const std::array<double, 3>& bgra) {
    const double b = bgra[0], g = bgra[1], r = bgra[2];
    if (r <= b || g <= b) return 0;
    const double maximum = std::max(r, g), chroma = maximum - b;
    const double hue = r >= g ? (g - b) / chroma : 2 - (r - b) / chroma; // 0=red, 1=yellow, 2=green
    const auto smooth = [](double t) { t = std::clamp(t, 0.0, 1.0); return t * t * (3 - 2 * t); };
    return (chroma / maximum) * smooth(hue * 2) * smooth((1.5 - hue) * 2);
}
// Color blend mode: B(base, team) = SetLum(team, Lum(base)).
// https://www.w3.org/TR/compositing-1/#blendingnonseparable
// Work in the PNG's encoded RGB space, not HSL lightness or linear-light RGB.
// Both buffers are premultiplied BGRA: unpremultiply before blending, then restore
// the original alpha. White in the mask applies the paint; black/transparent
// preserves the art. White/black and warm team paints also adjust luminosity.
inline void applyTeamColorMask(std::span<std::uint8_t> pixels, std::span<const std::uint8_t> mask, unsigned color) {
    if (pixels.size() != mask.size() || pixels.size() % 4)
        throw std::invalid_argument("Team mask must match the sprite dimensions");
    const std::array<double, 3> team{double(color & 255) / 255, double((color >> 8) & 255) / 255,
        double((color >> 16) & 255) / 255};
    const double teamLum = .11 * team[0] + .59 * team[1] + .30 * team[2];
    const std::array<double, 3> chroma{team[0] - teamLum, team[1] - teamLum, team[2] - teamLum};
    const double shadowLimit = -std::min({chroma[0], chroma[1], chroma[2]});
    const double highlightLimit = std::max({chroma[0], chroma[1], chroma[2]});
    const bool neutral = color == 0 || color == 0xffffff;
    const double warmLift = warmTeamHighlightStrength(team);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const unsigned coverage = std::max({mask[i], mask[i + 1], mask[i + 2]});
        const unsigned alpha = pixels[i + 3];
        if (!coverage || !alpha) continue;
        double luminosity = std::clamp((.11 * pixels[i] + .59 * pixels[i + 1] + .30 * pixels[i + 2]) / alpha, 0.0, 1.0);
        if (neutral) luminosity = neutralTeamLuminosity(luminosity, color == 0xffffff);
        else luminosity += warmLift * (neutralTeamLuminosity(luminosity, true) - luminosity);
        // ClipColor compresses chroma into gamut while preserving luminosity.
        // SetLum's RGB = luminosity + chroma; its clipping denominators depend
        // only on the team color. Their sum is <= 1, so the branches cannot overlap.
        double chromaScale = 1;
        if (luminosity < shadowLimit) chromaScale = luminosity / shadowLimit;
        else if (luminosity > 1 - highlightLimit) chromaScale = (1 - luminosity) / highlightLimit;
        const double opacity = double(coverage) / 255;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const double blended = luminosity + chroma[channel] * chromaScale;
            const double value = pixels[i + channel] * (1 - opacity) + blended * alpha * opacity;
            pixels[i + channel] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, double(alpha))));
        }
    }
}
}
