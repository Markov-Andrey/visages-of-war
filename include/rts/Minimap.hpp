#pragma once
#include "rts/Map.hpp"
#include "rts/Menu.hpp"

namespace rts {
// Same square grid and axes as the world, flattened to ground level.
// Rendering, mouse input and the camera outline share this transform.
class MinimapProjection {
public:
    MinimapProjection(UiRect area, const Map& map) : width_(map.width()), height_(map.height()) {
        view_.zoom = std::min(area.width / (WorldView::tileSize * width_), area.height / (WorldView::tileSize * height_));
        view_.origin = {area.x + (area.width - WorldView::tileSize * width_ * view_.zoom) * .5f,
                        area.y + (area.height - WorldView::tileSize * height_ * view_.zoom) * .5f};
    }
    Vec2 project(Vec2 world) const { return view_.project(world); }
    Vec2 unproject(Vec2 screen) const { return view_.unproject(screen); }
    std::optional<Cell> pick(Vec2 screen) const {
        const auto p = unproject(screen);
        if (p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_) return std::nullopt;
        return Cell{int(std::floor(p.x)), int(std::floor(p.y))};
    }
    std::vector<Vec2> viewport(const WorldView& camera, UiRect world) const {
        std::vector<Vec2> points{camera.unproject({world.x, world.y}), camera.unproject({world.x + world.width, world.y}),
            camera.unproject({world.x + world.width, world.y + world.height}), camera.unproject({world.x, world.y + world.height})};
        // Clip to map boundaries, including letterboxing of non-square maps.
        for (int edge = 0; edge < 4; ++edge) {
            if (points.empty()) break;
            const auto distance = [&](Vec2 p) { return edge == 0 ? p.x : edge == 1 ? width_ - p.x : edge == 2 ? p.y : height_ - p.y; };
            std::vector<Vec2> clipped;
            auto previous = points.back();
            float a = distance(previous);
            for (Vec2 current : points) {
                const float b = distance(current);
                if ((a >= 0) != (b >= 0)) clipped.push_back(previous + (current - previous) * (a / (a - b)));
                if (b >= 0) clipped.push_back(current);
                previous = current; a = b;
            }
            points = std::move(clipped);
        }
        for (auto& p : points) p = project(p);
        return points;
    }
private:
    int width_, height_;
    WorldView view_;
};
}
