#pragma once
#include "rts/Renderer.hpp"
#include "platform/WindowsSupport.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rts::render {
using platform::wide;
inline std::filesystem::path imagePath(const std::string& value) { return std::filesystem::path(std::u8string(value.begin(), value.end())); }
inline void check(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("Windows graphics operation failed: " + std::to_string(hr));
}
void applyImageTeamMask(IWICImagingFactory* wic, const std::filesystem::path& path,
    UINT width, UINT height, std::span<BYTE> pixels, unsigned color);
inline D2D1_POINT_2F point(Vec2 p) { return {p.x, p.y}; }
inline D2D1_RECT_F rect(float x, float y, float width, float height) { return {x, y, x + width, y + height}; }
inline void crystalMapMarker(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
    const WorldAssets& assets, const Crystal& crystal, Vec2 position) {
    const float radius = 1.5f + .5f * (crystal.width - 1);
    brush->SetColor(D2D1::ColorF(assets.crystalSprite(crystal.definitionId).glowColor));
    target->FillRectangle(rect(position.x - radius, position.y - radius, radius * 2, radius * 2), brush);
}
inline D2D1::Matrix3x2F surfaceTransform(const std::array<Vec2, 4>& top, Cell cell) {
    const auto u = top[1] - top[0], v = top[3] - top[0];
    return {u.x, u.y, v.x, v.y, top[0].x - u.x * cell.x - v.x * cell.y, top[0].y - u.y * cell.x - v.y * cell.y};
}
}
