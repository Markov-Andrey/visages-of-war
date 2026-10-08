#pragma once
#include "rts/Map.hpp"
#include "rts/Menu.hpp"

namespace rts {
// Same isometric grid and rectangular crop as the world, flattened to ground level.
// Rendering, mouse input and the camera outline share this transform.
class MinimapProjection {
public:
    MinimapProjection(UiRect area, const Map& map) : width_(map.width()), height_(map.height()), layout_(map.layoutSize()),
        minimum_(map.groundMinimum()), extent_(map.groundExtent()) {
        view_.zoom = std::min(area.width / extent_.x, area.height / extent_.y);
        view_.origin = {area.x + (area.width - extent_.x * view_.zoom) * .5f - minimum_.x * view_.zoom,
                        area.y + (area.height - extent_.y * view_.zoom) * .5f - minimum_.y * view_.zoom};
    }
    Vec2 project(Vec2 world) const { return view_.project(world); }
    Vec2 unproject(Vec2 screen) const { return view_.unproject(screen); }
    std::optional<Cell> pick(Vec2 screen) const {
        const auto p = unproject(screen);
        if (p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_) return std::nullopt;
        const Cell cell{int(std::floor(p.x)), int(std::floor(p.y))};
        return Map::completeCell(cell, layout_) ? std::optional<Cell>(cell) : std::nullopt;
    }
    std::vector<Vec2> viewport(const WorldView& camera, UiRect world) const {
        std::vector<Vec2> points{camera.unproject({world.x, world.y}), camera.unproject({world.x + world.width, world.y}),
            camera.unproject({world.x + world.width, world.y + world.height}), camera.unproject({world.x, world.y + world.height})};
        // Clip in projected space; a rectangular viewport remains rectangular.
        for (auto& p : points) p = WorldView{}.project(p);
        for (int edge = 0; edge < 4; ++edge) {
            if (points.empty()) break;
            const auto distance = [&](Vec2 p) { return edge == 0 ? p.x - minimum_.x : edge == 1 ? minimum_.x + extent_.x - p.x : edge == 2 ? p.y - minimum_.y : minimum_.y + extent_.y - p.y; };
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
        for (auto& p : points) p = view_.origin + p * view_.zoom;
        return points;
    }
private:
    int width_, height_;
    Cell layout_;
    Vec2 minimum_, extent_;
    WorldView view_;
};
}
