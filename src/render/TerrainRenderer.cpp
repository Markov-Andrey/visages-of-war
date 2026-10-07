#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::terrainRow(const Map& map, int row, const WorldView& view, bool grid, bool fog) {
    const auto drawEarly = [&](Cell cell) {
        if (cell.y == 0) return false;
        const auto& current = map.at(cell);
        const auto& previous = map.at(cell + Cell{0, -1});
        return current.height == previous.height && current.ramp == Cell{} && previous.ramp == Cell{};
    };
    const auto extent = size();
    const auto draw = [&](Cell cell) {
        const auto p = view.project(center(cell), float(map.at(cell).height));
        if (p.x <= -250 || p.x >= extent.x + 250 || p.y <= -100 || p.y >= extent.y + 180 || (fog && !fogMask_.covers(cell))) return;
        worldOpacity_ = 1;
        tile(map, cell, view, grid, fog);
    };
    for (int x = 0; x < map.width(); ++x)
        if (!drawEarly({x, row})) draw({x, row});
    // Paint the next row of continuous flat ground before this row's objects.
    // Lower-third anchors let rings/shadows spill across the cell boundary;
    // painting that ground later would erase their lower edge. Each tile is
    // still painted once, and cliffs/slopes retain their foreground ordering.
    if (row + 1 < map.height()) for (int x = 0; x < map.width(); ++x)
        if (drawEarly({x, row + 1})) draw({x, row + 1});
}

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
        // Flat neighbors have no cliff face. Stroking this degenerate polygon
        // leaks a half-pixel grid into the already painted neighboring water.
        if (face[0] == face[3] && face[1] == face[2]) continue;
        polygon(face, edge == 1 ? 0x454538 : 0x303c32);
        line(face[2], face[3], 0x242f29);
        lightSurface(face);
    }
    worldOpacity_ = previousOpacity;
    {
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
    if (tile.surface != Surface::Land) waterTile(map, c, view);
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
    // Grade the surface before fog so blue ambient light cannot tint unexplored black.
    lightSurface(top);
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
        else if (tile.surface != Surface::Land && map.at(adjacent).surface == Surface::Land)
            boundary(edge, 0xc5d3b3, std::max(1.0f, view.zoom), .4f);
        if (grid) boundary(edge, 0xabc494, 1.0f, .25f);
    }
}
}
