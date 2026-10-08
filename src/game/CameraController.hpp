#pragma once
#include "rts/Types.hpp"
#include <optional>

namespace rts::game {
// Presentation tuning: seconds and logical screen pixels, independent of simulation ticks.
struct CameraTuning {
    float edgeBand = 10;
    float panSpeed = 720;
    float accelerationTime = .10f;
    float brakingTime = .12f;
    float focusTime = .13f;
    float zoomTime = .12f;
    float zoomStep = 1.15f;
    float minimumZoom = .35f;
    float maximumZoom = 2;
    float initialZoom = .85f;
    float borderMargin = 96;
};
inline constexpr CameraTuning cameraTuning{};

class CameraController {
public:
    static Vec2 edgeDirection(Vec2 mouse, Vec2 extent);
    void setBounds(WorldView& view, Vec2 viewportOrigin, Vec2 viewportSize, Vec2 mapSize, Vec2 mapMinimum = {});
    void focus(WorldView& view, Vec2 world, Vec2 screenCenter, bool immediate = false);
    void zoom(WorldView& view, Vec2 anchor, float wheelSteps);
    void zoomTo(WorldView& view, Vec2 anchor, float zoom);
    void stop(const WorldView& view);
    void update(WorldView& view, Vec2 screenCenter, Vec2 direction, float elapsed);
private:
    struct Bounds { Vec2 origin, size, mapSize, mapMinimum; };
    std::optional<Bounds> bounds_;
    Vec2 boundedOrigin(const WorldView& view, Vec2 origin) const;
    void constrain(WorldView& view);
    Vec2 panVelocity_{}, focusVelocity_{}, zoomAnchor_{};
    std::optional<Vec2> focusWorld_;
    float zoomTarget_ = cameraTuning.initialZoom;
    float zoomVelocity_{};
};
}
