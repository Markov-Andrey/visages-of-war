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
    constrainCamera();
    camera_.focus(view_, world, cameraCenter(renderer_.size()), immediate);
}
void GameApplication::constrainCamera() {
    const auto extent = renderer_.size();
    if (extent.x <= 0 || extent.y <= 262) return;
    const auto area = BattleLayout(extent).world;
    camera_.setBounds(view_, {area.x, area.y}, {area.width, area.height}, game_.map().groundExtent(), game_.map().groundMinimum());
}
void GameApplication::resetCamera(bool immediate) {
    if (immediate) view_.zoom = cameraTuning.initialZoom;
    else camera_.zoomTo(view_, cameraCenter(renderer_.size()), cameraTuning.initialZoom);
    focus(center(game_.hall()) + Vec2{1, 2}, immediate);
}
bool GameApplication::cameraInputAllowed() const {
    return menu_.page == MenuPage::Playing && !ui_.consoleOpen && !panning_ && !minimapDragging_ && !dragging_ &&
        !(GetAsyncKeyState(VK_CONTROL) & 0x8000);
}
Vec2 GameApplication::cameraEdgeDirection() const {
    return cameraInputAllowed() ? CameraController::edgeDirection(mouse_, renderer_.size()) : Vec2{};
}
void GameApplication::moveCamera(float elapsed) {
    constrainCamera();
    if (GetForegroundWindow() != window_) { camera_.stop(view_); return; }
    Vec2 direction{};
    if (cameraInputAllowed()) {
        POINT screen{};
        if (GetCursorPos(&screen) && WindowFromPoint(screen) == window_) {
            POINT client = screen;
            if (ScreenToClient(window_, &client)) {
                const float scale = 96.0f / GetDpiForWindow(window_);
                mouse_ = {client.x * scale, client.y * scale};
                direction = cameraEdgeDirection();
            }
        }
        const auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
        direction.x = std::clamp(direction.x + float(down(VK_RIGHT)) - float(down(VK_LEFT)), -1.0f, 1.0f);
        direction.y = std::clamp(direction.y + float(down(VK_DOWN)) - float(down(VK_UP)), -1.0f, 1.0f);
    }
    camera_.update(view_, cameraCenter(renderer_.size()), direction, elapsed);
}
}
