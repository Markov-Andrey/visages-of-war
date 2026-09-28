#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::updateFogMask(const Simulation& game) {
    const bool changed = fogMask_.update(game.fog(), game.map().width(), game.map().height());
    if (!changed && fogBitmap_) return;
    const auto w = static_cast<UINT32>(fogMask_.pixelWidth()), h = static_cast<UINT32>(fogMask_.pixelHeight());
    const auto previousSize = fogBitmap_ ? fogBitmap_->GetPixelSize() : D2D1_SIZE_U{};
    if (!fogBitmap_ || previousSize.width != w || previousSize.height != h) {
        fogBrush_.Reset(); fogBitmap_.Reset();
        const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        check(target_->CreateBitmap(D2D1::SizeU(w, h), fogMask_.pixels().data(), w * 4, properties, fogBitmap_.GetAddressOf()));
        check(target_->CreateBitmapBrush(fogBitmap_.Get(),
            D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR), fogBrush_.GetAddressOf()));
        fogBrush_->SetTransform(D2D1::Matrix3x2F::Scale(1.0f / FogMask::pixelsPerCell, 1.0f / FogMask::pixelsPerCell) *
            D2D1::Matrix3x2F::Translation(-float(FogMask::padding), -float(FogMask::padding)));
    } else check(fogBitmap_->CopyFromMemory(nullptr, fogMask_.pixels().data(), w * 4));
}

void Renderer::tile(const Map& map, Cell c, const WorldView& view, bool grid, bool fog) {
    const auto& tile = map.at(c);
    const std::array<Vec2, 4> world{{{float(c.x), float(c.y)}, {c.x + 1.0f, float(c.y)},
        {c.x + 1.0f, c.y + 1.0f}, {float(c.x), c.y + 1.0f}}};
    const auto top = map.surfaceCorners(c, view);
    const float previousOpacity = worldOpacity_;
    if (fog) worldOpacity_ = fogMask_.lightAt(center(c));
    // Visible faces are drawn with the tile, allowing foreground cliffs to occlude units.
    for (const auto edge : {1, 2}) {
        const Cell neighbor = c + (edge == 1 ? Cell{1, 0} : Cell{0, 1});
        const int next = (edge + 1) % 4;
        const auto bottomAt = [&](int i) {
            const float height = map.contains(neighbor) ? map.surfaceHeight(neighbor, world[i]) : tile.height - .65f;
            const float topHeight = map.surfaceHeight(c, world[i]);
            return view.project(world[i], std::min(height, topHeight));
        };
        const std::array<Vec2, 4> face{{top[edge], top[next], bottomAt(next), bottomAt(edge)}};
        polygon(face, edge == 1 ? 0x454538 : 0x303c32);
        line(face[2], face[3], 0x242f29);
    }
    worldOpacity_ = previousOpacity;
    if (tile.surface == Surface::Land) {
        // The brush repeats in logical world coordinates across many cells. Only
        // the surface projection changes on slopes; the texture never restarts per tile.
        D2D1_MATRIX_3X2_F previous;
        target_->GetTransform(&previous);
        target_->SetTransform(surfaceTransform(top, c) * previous);
        groundBrush_->SetOpacity(worldOpacity_);
        // Adjacent cells share edges; antialiasing each fill separately would leave hairline seams.
        const auto antialias = target_->GetAntialiasMode();
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        target_->FillRectangle(rect(float(c.x), float(c.y), 1, 1), groundBrush_.Get());
        target_->SetAntialiasMode(antialias);
        target_->SetTransform(previous);
    } else {
        const bool shallow = tile.surface == Surface::ShallowWater;
        polygon(top, shallow ? 0x4e9fa5 : 0x245879);
        // Small wavelets distinguish water even with the navigation grid hidden.
        for (int i = 0; i < 3; ++i) {
            const float offset = ((c.x * 7 + c.y * 3 + i * 5) % 7) * .045f;
            const Vec2 a{c.x + .12f + offset, c.y + .2f + i * .27f};
            const Vec2 b{a.x + .27f, a.y};
            line(view.project(a, map.surfaceHeight(c, a)), view.project(b, map.surfaceHeight(c, b)), shallow ? 0x83c5bd : 0x3c7f9d, view.zoom);
        }
    }
    {
        D2D1_MATRIX_3X2_F previous;
        target_->GetTransform(&previous);
        target_->SetTransform(surfaceTransform(top, c) * previous);
        const auto antialias = target_->GetAntialiasMode();
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        paintedTile(c);
        target_->SetAntialiasMode(antialias);
        target_->SetTransform(previous);
    }
    if (tile.ramp != Cell{}) {
        polygon(top, 0x8c8260, .7f);
        for (int i = 1; i < 5; ++i) {
            const float t = i / 5.0f;
            Vec2 a{}, b{};
            if (tile.ramp.x) { a = {c.x + t, float(c.y)}; b = {c.x + t, c.y + 1.0f}; }
            else { a = {float(c.x), c.y + t}; b = {c.x + 1.0f, c.y + t}; }
            line(view.project(a, map.surfaceHeight(c, a)), view.project(b, map.surfaceHeight(c, b)), 0xaaa079);
        }
    }
    if (fog) {
        D2D1_MATRIX_3X2_F previous;
        target_->GetTransform(&previous);
        target_->SetTransform(surfaceTransform(top, c) * previous);
        const auto antialias = target_->GetAntialiasMode();
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        target_->FillRectangle(rect(float(c.x), float(c.y), 1, 1), fogBrush_.Get());
        target_->SetAntialiasMode(antialias);
        target_->SetTransform(previous);
    }
    // Boundary strokes extend outside the surface fill. Apply fog to the strokes
    // themselves so shore and grid outlines cannot leak into unexplored terrain.
    const auto boundary = [&](size_t edge, unsigned color, float width, float opacity = 1.0f) {
        const size_t next = (edge + 1) % 4;
        const int segments = fog ? FogMask::pixelsPerCell : 1;
        for (int i = 0; i < segments; ++i) {
            const float a = float(i) / segments, b = float(i + 1) / segments;
            const Vec2 sample = world[edge] + (world[next] - world[edge]) * ((a + b) * .5f);
            worldOpacity_ = previousOpacity * opacity * (fog ? fogMask_.lightAt(sample) : 1.0f);
            line(top[edge] + (top[next] - top[edge]) * a, top[edge] + (top[next] - top[edge]) * b, color, width);
        }
        worldOpacity_ = previousOpacity;
    };
    constexpr std::array<Cell, 4> neighbors{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
    for (size_t edge = 0; edge < neighbors.size(); ++edge) {
        const Cell adjacent = c + neighbors[edge];
        if (!map.contains(adjacent) || map.at(adjacent).height != tile.height)
            boundary(edge, 0xaca17a, std::max(1.0f, 2 * view.zoom));
        else if (tile.surface != Surface::Land && map.at(adjacent).surface != tile.surface)
            boundary(edge, map.at(adjacent).surface == Surface::Land ? 0xc5c299 : 0x70b8bd, std::max(1.0f, 2 * view.zoom));
        if (grid) boundary(edge, 0xabc494, 1.0f, .25f);
    }
}
}
