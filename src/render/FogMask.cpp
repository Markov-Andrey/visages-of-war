#include "rts/FogMask.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace rts {
bool FogMask::update(const FogOfWar& fog, int width, int height) {
    if (width < 1 || height < 1 || width > 512 || height > 512) throw std::invalid_argument("Invalid fog mask size");
    scratch_.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) scratch_[static_cast<size_t>(y) * width + x] = fog.at({x, y});
    if (width == width_ && height == height_ && scratch_ == cells_) return false;
    width_ = width; height_ = height; cells_.swap(scratch_);
    const int w = pixelWidth(), h = pixelHeight();
    const auto value = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0.0f;
        const auto v = cells_[static_cast<size_t>(y) * width + x];
        return v == Visibility::Visible ? 1.0f : v == Visibility::Explored ? .3f : 0.0f;
    };
    std::vector<float> raw(static_cast<size_t>(w) * h), horizontal(raw.size());
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const float wx = (x + .5f) / pixelsPerCell - padding - .5f;
        const float wy = (y + .5f) / pixelsPerCell - padding - .5f;
        const int cx = int(std::floor(wx)), cy = int(std::floor(wy));
        const float a = wx - cx, b = wy - cy;
        raw[static_cast<size_t>(y) * w + x] = std::lerp(std::lerp(value(cx, cy), value(cx + 1, cy), a),
            std::lerp(value(cx, cy + 1), value(cx + 1, cy + 1), a), b);
    }
    // A short separable Gaussian rounds cell corners without changing game visibility.
    constexpr std::array<float, 5> kernel{1 / 16.0f, 4 / 16.0f, 6 / 16.0f, 4 / 16.0f, 1 / 16.0f};
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        for (int k = -2; k <= 2; ++k) if (x + k >= 0 && x + k < w)
            horizontal[static_cast<size_t>(y) * w + x] += raw[static_cast<size_t>(y) * w + x + k] * kernel[k + 2];
    light_.assign(raw.size(), 0);
    pixels_.resize(raw.size());
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const size_t index = static_cast<size_t>(y) * w + x;
        for (int k = -2; k <= 2; ++k) if (y + k >= 0 && y + k < h)
            light_[index] += horizontal[static_cast<size_t>(y + k) * w + x] * kernel[k + 2];
        const auto alpha = static_cast<uint32_t>(std::lround((1 - std::clamp(light_[index], 0.0f, 1.0f)) * 255));
        // Premultiplied BGRA, matching the renderer's unexplored background #081119.
        pixels_[index] = (alpha << 24) | ((8 * alpha / 255) << 16) | ((17 * alpha / 255) << 8) | (25 * alpha / 255);
    }
    ++revision_;
    return true;
}
float FogMask::lightAt(Vec2 world) const {
    if (light_.empty()) return 0;
    const float px = (world.x + padding) * pixelsPerCell - .5f, py = (world.y + padding) * pixelsPerCell - .5f;
    const int x = int(std::floor(px)), y = int(std::floor(py));
    const auto sample = [&](int sx, int sy) {
        return sx < 0 || sy < 0 || sx >= pixelWidth() || sy >= pixelHeight() ? 0.0f : light_[static_cast<size_t>(sy) * pixelWidth() + sx];
    };
    return std::lerp(std::lerp(sample(x, y), sample(x + 1, y), px - x),
        std::lerp(sample(x, y + 1), sample(x + 1, y + 1), px - x), py - y);
}
bool FogMask::covers(Cell cell) const {
    for (int y = cell.y - 1; y <= cell.y + 1; ++y) for (int x = cell.x - 1; x <= cell.x + 1; ++x)
        if (x >= 0 && y >= 0 && x < width_ && y < height_ && cells_[static_cast<size_t>(y) * width_ + x] != Visibility::Unexplored) return true;
    return false;
}
}
