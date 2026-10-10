#include "RenderSupport.hpp"
#include "rts/TerrainVisibility.hpp"
#include "TerrainGrid.hpp"

namespace rts {
using namespace render;
void Renderer::gridFootprint(const Map& map, Cell cell, const WorldView& view, unsigned color) {
    const WorldView local{};
    const auto centerAtOne = local.project(center(cell), float(map.at(cell).height));
    auto corners = map.surfaceCorners(cell, local);
    std::array<float, 8> key{};
    for (size_t i = 0; i < corners.size(); ++i) {
        corners[i] = (corners[i] - centerAtOne) * .85f;
        key[i * 2] = corners[i].x;
        key[i * 2 + 1] = corners[i].y;
    }
    auto& geometry = gridFootprints_[key];
    if (!geometry) {
        ComPtr<ID2D1GeometrySink> sink;
        check(factory_->CreatePathGeometry(geometry.GetAddressOf()));
        check(geometry->Open(sink.GetAddressOf()));
        sink->BeginFigure(point(corners.front()), D2D1_FIGURE_BEGIN_HOLLOW);
        for (size_t i = 1; i < corners.size(); ++i) sink->AddLine(point(corners[i]));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        check(sink->Close());
    }
    const auto p = view.project(center(cell), float(map.at(cell).height));
    D2D1_MATRIX_3X2_F previous;
    target_->GetTransform(&previous);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(view.zoom, view.zoom) *
        D2D1::Matrix3x2F::Translation(p.x, p.y) * previous);
    brush_->SetColor(D2D1::ColorF(color, .7f * worldOpacity_));
    target_->DrawGeometry(geometry.Get(), brush_.Get(), 1.f / view.zoom);
    target_->SetTransform(previous);
}

void Renderer::terrainRow(const Map& map, int row, const WorldView& view, bool grid, bool fog) {
    const bool cropped = map.layoutSize() != Cell{};
    if (cropped) {
        const auto a = view.origin + map.groundMinimum() * view.zoom;
        const auto e = map.groundExtent() * view.zoom;
        target_->PushAxisAlignedClip(rect(a.x, a.y, e.x, e.y), D2D1_ANTIALIAS_MODE_ALIASED);
    }
    const auto drawEarly = [&](Cell cell) {
        if (cell.x == 0 || cell.y == 0) return false;
        const auto& current = map.at(cell);
        if (current.ramp != Cell{}) return false;
        for (Cell d : {Cell{-1, 0}, Cell{0, -1}}) {
            const auto& previous = map.at(cell + d);
            if (current.height != previous.height || previous.ramp != Cell{}) return false;
        }
        return true;
    };
    const auto extent = size();
    const auto draw = [&](Cell cell) {
        if (!terrainCellVisible(map, cell, view, extent, fog ? &fogMask_ : nullptr)) return;
        worldOpacity_ = 1;
        tile(map, cell, view, grid, fog);
    };
    const auto diagonal = [&](int depth, bool early) {
        const auto columns = terrainColumns(view, map.width(), map.height(), depth, -250, extent.x + 250);
        for (int x = columns.begin; x < columns.end; ++x) {
            const Cell c{x, depth - x};
            if (drawEarly(c) == early) draw(c);
        }
    };
    diagonal(row, false);
    // Paint continuous foreground ground before rings, shadows and sprite feet.
    // Cliffs and ramps retain their diagonal painter order.
    diagonal(row + 1, true);
    if (cropped) target_->PopAxisAlignedClip();
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
    } else {
        const auto area = fogMask_.changedRegion();
        const auto destination = D2D1::RectU(area.left, area.top, area.right, area.bottom);
        check(fogBitmap_->CopyFromMemory(&destination,
            fogMask_.pixels().data() + static_cast<size_t>(area.top) * w + area.left, w * 4));
    }
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
    if (map.layoutSize() != Cell{} && !map.playable(c)) polygon(top, 0x29352b, .45f);
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
    if (grid) terrainGrid(c, tile.ramp, view, top);
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
        boundaryRuns(world[edge], world[next], fog ? &fogMask_ : nullptr, [&](float a, float b, float light) {
            worldOpacity_ = previousOpacity * opacity * light;
            line(top[edge] + (top[next] - top[edge]) * a, top[edge] + (top[next] - top[edge]) * b, color, width);
        });
        worldOpacity_ = previousOpacity;
    };
    constexpr std::array<Cell, 4> neighbors{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
    for (size_t edge = 0; edge < neighbors.size(); ++edge) {
        const Cell adjacent = c + neighbors[edge];
        if (!map.contains(adjacent) || map.at(adjacent).height != tile.height)
            boundary(edge, 0xaca17a, std::max(1.0f, 2 * view.zoom));
        else if (tile.surface != Surface::Land && map.at(adjacent).surface == Surface::Land)
            boundary(edge, 0xc5d3b3, std::max(1.0f, view.zoom), .4f);
    }
}
}
