#include "GameApplication.hpp"
#include <algorithm>

namespace rts::game {
namespace {
Vec2 cameraCenter(Vec2 extent) {
    const auto area = BattleLayout(extent).world;
    return {area.x + area.width * .5f, area.y + area.height * .5f};
}
}
void GameApplication::focus(Vec2 world, bool immediate) {
    camera_.focus(view_, world, cameraCenter(renderer_.size()), immediate);
}
void GameApplication::resetCamera(bool immediate) {
    if (immediate) view_.zoom = cameraTuning.initialZoom;
    else camera_.zoomTo(view_, cameraCenter(renderer_.size()), cameraTuning.initialZoom);
    focus(center(game_.hall()) + Vec2{1, 2}, immediate);
}
void GameApplication::moveCamera(float elapsed) {
    if (GetForegroundWindow() != window_) { camera_.stop(view_); return; }
    Vec2 direction{};
    if (!panning_ && !minimapDragging_ && !dragging_ && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
        POINT screen{};
        if (GetCursorPos(&screen) && WindowFromPoint(screen) == window_) {
            POINT client = screen;
            if (ScreenToClient(window_, &client)) {
                const float scale = 96.0f / GetDpiForWindow(window_);
                mouse_ = {client.x * scale, client.y * scale};
                direction = CameraController::edgeDirection(mouse_, renderer_.size());
            }
        }
        const auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
        direction.x = std::clamp(direction.x + float(down(VK_RIGHT)) - float(down(VK_LEFT)), -1.0f, 1.0f);
        direction.y = std::clamp(direction.y + float(down(VK_DOWN)) - float(down(VK_UP)), -1.0f, 1.0f);
    }
    camera_.update(view_, cameraCenter(renderer_.size()), direction, elapsed);
}
}
