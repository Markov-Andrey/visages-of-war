#include "rts/NightLighting.hpp"
#include "rts/GameplayUi.hpp"
#include <algorithm>
#include <cmath>

namespace rts {
float flamePulse(const BuildingSpriteLayer& layer, std::uint64_t ticks) {
    const auto frame = (ticks / layer.ticksPerFrame + layer.phase) % layer.frames.size();
    const double angle = frame * 6.283185307179586 / layer.frames.size();
    return .5f + .5f * float(.65 * std::sin(angle) + .35 * std::sin(angle * 2 + .7));
}
float crystalPulse(EntityId id, std::uint64_t ticks) {
    return .88f + .035f * float(std::sin(ticks * .035 + (id % 31)));
}
std::uint8_t emissionCoverage(std::uint32_t mask) {
    // RGB already contains alpha: multiplying by alpha again would crush soft edges.
    return static_cast<std::uint8_t>((((mask >> 16) & 255) * 54 + ((mask >> 8) & 255) * 183 + (mask & 255) * 19 + 128) / 256);
}
std::uint8_t highlightEmission(std::uint32_t pixel) {
    const unsigned alpha = pixel >> 24;
    if (!alpha) return 0;
    const float brightness = std::max({pixel & 255, (pixel >> 8) & 255, (pixel >> 16) & 255}) / float(alpha);
    const float t = std::clamp((brightness - .32f) / .48f, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(255 * t * t * (3 - 2 * t)));
}
float lightFalloff(float distanceSquared, float radius) {
    if (radius <= 0) return 0;
    const float d = distanceSquared / (radius * radius);
    const float wide = std::max(0.0f, 1 - d), core = std::max(0.0f, 1 - d * 12);
    return .65f * wide * wide + .35f * core * core;
}
float nightStrength(const WorldClock& clock) {
    const float minute = static_cast<float>(clock.minuteOfDay());
    const auto smooth = [](float t) { return t * t * (3 - 2 * t); };
    if (minute >= 300 && minute < 420) return 1 - smooth((minute - 300) / 120);
    if (minute >= 1020 && minute < 1140) return smooth((minute - 1020) / 120);
    return clock.phase() == DayPhase::Night ? 1.0f : 0.0f;
}
std::vector<ProjectedLight> buildingLights(const Simulation& game, const WorldView& view) {
    std::vector<ProjectedLight> lights;
    for (const auto& b : game.buildings()) {
        if (!b.complete() || b.health <= 0) continue;
        const auto* stage = b.definition.buildingSprite.stage(b.constructionProgress, b.definition.constructionTicks);
        if (!stage || stage->lights.empty()) continue;
        bool visible = false;
        for (int y = 0; y < b.definition.height; ++y) for (int x = 0; x < b.definition.width; ++x)
            visible |= game.fog().visible(b.origin + Cell{x, y});
        if (!visible) continue; // A remembered building must not disclose its current activity.
        const auto bounds = buildingBounds(game, b, view);
        const float scale = b.definition.buildingSprite.scale * view.zoom;
        size_t index = 0;
        for (const auto& light : stage->lights) {
            const float phase = float((b.id * 17 + index++ * 7) % 101);
            if (!light.visible(b.training()) || light.intensity <= 0) continue;
            const double ticks = static_cast<double>(game.clock().elapsedTicks());
            float pulse = .5f + .5f * float(.65 * std::sin(ticks * .19 + phase) + .35 * std::sin(ticks * .47 + phase * 1.7));
            if (light.animationLayer >= 0) {
                const auto& layer = stage->layers.at(light.animationLayer);
                // Same animation frame always produces the same exposure, including a pause.
                pulse = flamePulse(layer, game.clock().elapsedTicks());
            }
            const float intensity = light.intensity * (1 - light.flicker * pulse);
            lights.push_back({Vec2{bounds.x, bounds.y} + light.position * scale,
                light.radius * WorldView::tileSize * view.zoom, intensity, light.color});
        }
    }
    return lights;
}
std::vector<ProjectedLight> crystalLights(const Simulation& game, const WorldView& view) {
    std::vector<ProjectedLight> lights;
    for (const auto& crystal : game.crystals()) {
        if (crystal.remaining <= 0 || !game.fog().visible(crystal.cell)) continue;
        const auto ground = view.project(center(crystal.cell), float(game.map().at(crystal.cell).height));
        const float intensity = crystalPulse(crystal.id, game.clock().elapsedTicks());
        lights.push_back({ground + Vec2{0, -22} * view.zoom, 2.1f * WorldView::tileSize * view.zoom,
            intensity, 0xbd8fff});
    }
    return lights;
}
namespace {
std::uint32_t overlayPixel(float night, float light, unsigned tint) {
    // Cool ambient light keeps texture contrast; compact warm cores sit in a wider faint pool.
    const float dark = night * .48f * (1 - light * .88f), warm = night * .18f * light;
    const float alpha = warm + dark * (1 - warm);
    const auto component = [&](unsigned a, unsigned b) {
        return static_cast<std::uint32_t>(std::lround(a * dark * (1 - warm) + b * warm));
    };
    return (static_cast<std::uint32_t>(std::lround(alpha * 255)) << 24) |
        (component(0x18, (tint >> 16) & 255) << 16) | (component(0x29, (tint >> 8) & 255) << 8) |
        component(0x50, tint & 255);
}
}
void NightLightingRaster::update(const Simulation& game, const WorldView& view, Vec2 extent, const FogMask& fog,
    std::span<const ProjectedLight> additionalLights) {
    width_ = std::max(1, static_cast<int>(std::ceil(extent.x / pixelStep)));
    height_ = std::max(1, static_cast<int>(std::ceil(extent.y / pixelStep)));
    const float night = nightStrength(game.clock());
    pixels_.assign(static_cast<size_t>(width_) * height_, overlayPixel(night, 0, 0));
    visibilityPixels_.assign(pixels_.size(), 0);
    if (night <= 0) return;
    auto lights = buildingLights(game, view);
    const auto crystals = crystalLights(game, view);
    lights.insert(lights.end(), crystals.begin(), crystals.end());
    lights.insert(lights.end(), additionalLights.begin(), additionalLights.end());
    std::erase_if(lights, [&](const auto& light) {
        return light.position.x + light.radius < 0 || light.position.y + light.radius < 0 ||
            light.position.x - light.radius > extent.x || light.position.y - light.radius > extent.y;
    });
    if (lights.empty()) return;
    visibility_.assign(pixels_.size(), 0);
    const Vec2 step{extent.x / width_, extent.y / height_};
    const auto& map = game.map();
    // Rasterize visible ground surfaces in the same order as terrain, including ramps.
    // This avoids a flat-height screen-to-map lookup lighting through a foreground cliff.
    const auto rasterize = [&](const std::array<Vec2, 4>& corners, Cell cell, bool surface) {
        float x0 = corners[0].x, x1 = x0, y0 = corners[0].y, y1 = y0;
        for (const auto p : corners) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
        const int left = std::clamp(int(std::floor(x0 / step.x)), 0, width_), right = std::clamp(int(std::ceil(x1 / step.x)), 0, width_);
        const int top = std::clamp(int(std::floor(y0 / step.y)), 0, height_), bottom = std::clamp(int(std::ceil(y1 / step.y)), 0, height_);
        if (left >= right || top >= bottom) return;
        const auto u = corners[1] - corners[0], v = corners[3] - corners[0];
        const float determinant = u.x * v.y - u.y * v.x;
        if (std::abs(determinant) < .001f) return;
        for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
            const Vec2 p = Vec2{(x + .5f) * step.x, (y + .5f) * step.y} - corners[0];
            const float a = (p.x * v.y - p.y * v.x) / determinant, b = (u.x * p.y - u.y * p.x) / determinant;
            if (a < 0 || b < 0 || a >= 1 || b >= 1) continue;
            // Only presentation feathering is sampled here; hidden emitters were rejected above.
            const Vec2 world = surface ? Vec2{cell.x + a, cell.y + b} : center(cell);
            visibility_[static_cast<size_t>(y) * width_ + x] = std::clamp((fog.lightAt(world) - .3f) / .7f, 0.0f, 1.0f);
        }
    };
    for (int y = 0; y < map.height(); ++y) for (int x = 0; x < map.width(); ++x) {
        const Cell c{x, y};
        const auto top = map.surfaceCorners(c, view);
        // The front face is a vertical strip in the square projection.
        const Cell below{x, y + 1};
        const std::array<Vec2, 2> world{{{x + 1.0f, y + 1.0f}, {float(x), y + 1.0f}}};
        std::array<Vec2, 4> face{top[2], top[3], {}, {}};
        for (size_t i = 0; i < 2; ++i) {
            const float h = map.contains(below) ? map.surfaceHeight(below, world[i]) : map.at(c).height - .65f;
            face[3 - i] = view.project(world[i], std::min(h, map.surfaceHeight(c, world[i])));
        }
        rasterize(face, c, false);
        rasterize(top, c, true);
    }
    for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) {
        const size_t index = static_cast<size_t>(y) * width_ + x;
        visibilityPixels_[index] = static_cast<std::uint32_t>(std::lround(visibility_[index] * 255)) * 0x01010101u;
        if (visibility_[index] <= 0) continue;
        const Vec2 p{(x + .5f) * step.x, (y + .5f) * step.y};
        float strength = 0, total = 0, red = 0, green = 0, blue = 0;
        for (const auto& light : lights) {
            const auto d = p - light.position;
            const float amount = std::clamp(light.intensity, 0.0f, 1.0f) * lightFalloff(d.x * d.x + d.y * d.y, light.radius);
            strength = std::max(strength, amount); // Overlap must not burn out the scene.
            total += amount;
            red += ((light.color >> 16) & 255) * amount;
            green += ((light.color >> 8) & 255) * amount;
            blue += (light.color & 255) * amount;
        }
        if (total <= 0) continue;
        const unsigned tint = (static_cast<unsigned>(red / total) << 16) | (static_cast<unsigned>(green / total) << 8) | static_cast<unsigned>(blue / total);
        pixels_[index] = overlayPixel(night, strength * visibility_[index], tint);
    }
}
}
