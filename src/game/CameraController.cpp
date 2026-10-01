#include "CameraController.hpp"
#include <algorithm>
#include <cmath>

namespace rts::game {
namespace {
// Exact critically damped spring for a stationary target. No frame-count based interpolation.
void approach(float& value, float& velocity, float target, float time, float elapsed) {
    const float omega = 2 / time, offset = value - target;
    const float decay = std::exp(-omega * elapsed);
    const float change = (velocity + omega * offset) * elapsed;
    value = target + (offset + change) * decay;
    velocity = (velocity - omega * change) * decay;
}
}
Vec2 CameraController::edgeDirection(Vec2 mouse, Vec2 extent) {
    if (mouse.x < 0 || mouse.y < 0 || mouse.x >= extent.x || mouse.y >= extent.y) return {};
    const auto axis = [](float position, float size) {
        if (position < cameraTuning.edgeBand) return -1.0f;
        if (position >= size - cameraTuning.edgeBand) return 1.0f;
        return 0.0f;
    };
    return {axis(mouse.x, extent.x), axis(mouse.y, extent.y)};
}
void CameraController::stop(const WorldView& view) {
    panVelocity_ = {}; focusVelocity_ = {}; focusWorld_.reset();
    zoomTarget_ = view.zoom; zoomVelocity_ = 0;
}
void CameraController::focus(WorldView& view, Vec2 world, Vec2 screenCenter, bool immediate) {
    panVelocity_ = {};
    if (!focusWorld_) focusVelocity_ = {};
    zoomAnchor_ = screenCenter;
    if (immediate) {
        stop(view);
        view.origin = view.origin + screenCenter - view.project(world, 0);
    } else focusWorld_ = world;
}
void CameraController::zoomTo(WorldView& view, Vec2 anchor, float zoom) {
    focusWorld_.reset(); focusVelocity_ = {};
    zoomAnchor_ = anchor;
    zoomTarget_ = std::clamp(zoom, cameraTuning.minimumZoom, cameraTuning.maximumZoom);
    // A reversed wheel input must not keep travelling in the previous direction.
    if ((zoomTarget_ - view.zoom) * zoomVelocity_ < 0) zoomVelocity_ = 0;
}
void CameraController::zoom(WorldView& view, Vec2 anchor, float wheelSteps) {
    zoomTo(view, anchor, zoomTarget_ * std::pow(cameraTuning.zoomStep, wheelSteps));
}
void CameraController::update(WorldView& view, Vec2 screenCenter, Vec2 direction, float elapsed) {
    if (!std::isfinite(elapsed) || elapsed <= 0) return;
    const float length = std::hypot(direction.x, direction.y);
    if (length > 1) direction = direction * (1 / length);
    const bool moving = length > 0;
    if (moving) { focusWorld_.reset(); focusVelocity_ = {}; }

    const float oldZoom = view.zoom;
    approach(view.zoom, zoomVelocity_, zoomTarget_, cameraTuning.zoomTime, elapsed);
    view.zoom = std::clamp(view.zoom, cameraTuning.minimumZoom, cameraTuning.maximumZoom);
    if (std::abs(view.zoom - zoomTarget_) < .00001f && std::abs(zoomVelocity_) < .0001f) {
        view.zoom = zoomTarget_; zoomVelocity_ = 0;
    }
    // Keep the currently visible point under the last wheel position throughout the zoom.
    if (view.zoom != oldZoom) {
        view.origin = zoomAnchor_ - (zoomAnchor_ - view.origin) * (view.zoom / oldZoom);
        focusVelocity_ = focusVelocity_ * (view.zoom / oldZoom);
    }

    if (focusWorld_) {
        const Vec2 target = view.origin + screenCenter - view.project(*focusWorld_, 0);
        approach(view.origin.x, focusVelocity_.x, target.x, cameraTuning.focusTime, elapsed);
        approach(view.origin.y, focusVelocity_.y, target.y, cameraTuning.focusTime, elapsed);
        if (std::hypot(view.origin.x - target.x, view.origin.y - target.y) < .05f &&
            std::hypot(focusVelocity_.x, focusVelocity_.y) < .05f) {
            view.origin = target; focusVelocity_ = {}; focusWorld_.reset();
        }
        return;
    }
    const Vec2 desired = direction * cameraTuning.panSpeed;
    const float time = moving ? cameraTuning.accelerationTime : cameraTuning.brakingTime;
    const float decay = std::exp(-elapsed / time);
    const Vec2 difference = panVelocity_ - desired;
    // Integrating the velocity curve keeps acceleration and stopping distance consistent at different FPS.
    view.origin = view.origin - (desired * elapsed + difference * (time * (1 - decay)));
    panVelocity_ = desired + difference * decay;
    if (!moving && std::hypot(panVelocity_.x, panVelocity_.y) < .05f) panVelocity_ = {};
}
}
