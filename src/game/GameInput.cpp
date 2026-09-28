#include "GameApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace rts::game {
void GameApplication::focus(rts::Vec2 world) {
    const auto area = rts::BattleLayout(renderer_.size()).world;
    const rts::Vec2 middle{area.width * .5f, area.y + area.height * .5f};
    view_.origin = view_.origin + middle - view_.project(world, 0);
}

rts::CursorKind GameApplication::cursorKind() const {
    using rts::CursorKind;
    if (menu_.page != rts::MenuPage::Playing) {
        const rts::MenuLayout layout(renderer_.size(), menu_.canResume);
        if (menu_.page == rts::MenuPage::Main) {
            if (layout.battles.contains(mouse_) || layout.exit.contains(mouse_) || (menu_.canResume && layout.resume.contains(mouse_))) return CursorKind::Hand;
        } else {
            if (menu_.commanderDropdown) {
                const auto count = std::min(size_t{5}, definitions_.commanders().size() - menu_.commanderScroll);
                if (rts::UiRect{layout.commanderOption.x, layout.commanderOption.y, layout.commanderOption.width, count * 44.0f}.contains(mouse_)) return CursorKind::Hand;
            }
            if (layout.back.contains(mouse_) || layout.commander.contains(mouse_) || (menu_.mapSelected && layout.start.contains(mouse_))) return CursorKind::Hand;
            for (const auto& color : layout.colors) if (color.contains(mouse_)) return CursorKind::Hand;
        }
        return CursorKind::Default;
    }
    if (panning_) return CursorKind::Move;
    const rts::BattleLayout layout(renderer_.size());
    const auto* building = selectedBuilding();
    const auto selected = [&](auto predicate) {
        return std::any_of(ui_.selection.ids.begin(), ui_.selection.ids.end(), [&](rts::EntityId id) {
            const auto* u = game_.unit(id);
            return u && u->owner == game_.player().id && predicate(*u);
        });
    };
    if (layout.menu.contains(mouse_) || layout.army.contains(mouse_)) return CursorKind::Hand;
    if (layout.hero.contains(mouse_)) return game_.hero() ? CursorKind::Hand : CursorKind::Blocked;
    const bool minimap = layout.minimap.contains(mouse_);
    if (!mouseInWorld() && !minimap) {
        for (size_t i = 0; i < layout.commands.size(); ++i) if (layout.commands[i].contains(mouse_)) {
            bool active = false;
            if (building) active = (building->complete() && ((i == 1 && !building->definition.trainableUnits.empty()) ||
                ((i == 0 || i == 2 || i == 4) && i / 2 < building->definition.trainableUnits.size()))) ||
                (i == 5 && (!building->complete() || !building->production.empty()));
            else active = (i == 3 && selected([](const rts::Unit&) { return true; })) ||
                (i < 3 && i < game_.buildingTypes().size() && selected([](const rts::Unit& u) { return u.definition.canBuild; }));
            return active ? CursorKind::Hand : CursorKind::Default;
        }
        return CursorKind::Default;
    }
    const auto target = minimap ? layout.minimapCell(mouse_, game_.map()) :
        (!ui_.placement.empty() || ui_.rallyMode || building ? game_.map().pick(mouse_, view_) : pickCommandTarget());
    if (!ui_.placement.empty() && !minimap) {
        const auto& type = game_.entityType(ui_.placement);
        return target && game_.canPlace(type.id, *target) && game_.storedCrystals() >= type.cost.crystals && game_.armySupply().canReserve(type.cost.supply) ? CursorKind::Build : CursorKind::Blocked;
    }
    if (building && (ui_.rallyMode || !building->definition.trainableUnits.empty())) {
        if (building->definition.trainableUnits.empty()) return CursorKind::Default;
        return target && std::all_of(building->definition.trainableUnits.begin(),building->definition.trainableUnits.end(),
            [&](const auto& id) { return game_.map().walkable(*target,game_.entityType(id).movement); }) ? CursorKind::Rally : CursorKind::Blocked;
    }
    if (minimap) return CursorKind::Hand;
    if (!target) return CursorKind::Default;
    for (const auto& enemy : game_.units()) if (enemy.owner != game_.player().id && enemy.cell == *target && game_.fog().visible(enemy.cell)) {
        if (!selected([](const rts::Unit&) { return true; })) return CursorKind::Select;
        return selected([&](const rts::Unit& u) { return game_.canAttack(u, enemy); }) ? CursorKind::Attack : CursorKind::Blocked;
    }
    for (const auto& crystal : game_.crystals()) if (crystal.cell == *target && crystal.remaining > 0 && game_.fog().visible(*target)) {
        if (selected([](const rts::Unit& u) { return u.definition.carryCapacity > 0; })) return CursorKind::Gather;
    }
    if (const auto id = rts::pickEntity(game_, view_, mouse_)) {
        const auto* site = game_.building(*id);
        if (site && !site->complete() && selected([](const rts::Unit& u) { return u.definition.canBuild; })) return CursorKind::Build;
        return CursorKind::Select;
    }
    if (selected([](const rts::Unit&) { return true; })) {
        // Never reveal hidden terrain restrictions via the pointer.
        if (!game_.fog().explored(*target)) return CursorKind::Default;
        return selected([&](const rts::Unit& u) { return game_.map().walkable(*target, u.definition.movement); }) ? CursorKind::Default : CursorKind::Blocked;
    }
    return CursorKind::Default;
}

void GameApplication::refreshCursor() {
    POINT p{};
    if (GetForegroundWindow() == window_ && GetCursorPos(&p) && (GetCapture() == window_ || WindowFromPoint(p) == window_))
        SetCursor(cursor_.handle(GetDpiForWindow(window_), cursorKind()));
}

bool GameApplication::mouseInWorld() const {
    const rts::BattleLayout layout(renderer_.size());
    return layout.world.contains(mouse_) && !layout.army.contains(mouse_) && !layout.hero.contains(mouse_);
}

void GameApplication::resetCamera() {
    view_.zoom = .85f;
    focus(rts::center(game_.hall()) + rts::Vec2{1, 2});
}

rts::Vec2 GameApplication::mousePosition(LPARAM lParam) const {
    const float scale = 96.0f / GetDpiForWindow(window_);
    return {GET_X_LPARAM(lParam) * scale, GET_Y_LPARAM(lParam) * scale};
}

void GameApplication::moveCamera(float dt) {
    if (GetForegroundWindow() != window_ || (GetAsyncKeyState(VK_CONTROL) & 0x8000)) return;
    const auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
    const float speed = 580.0f * dt;
    if (down('A') || down(VK_LEFT)) view_.origin.x += speed;
    if (down('D') || down(VK_RIGHT)) view_.origin.x -= speed;
    if (down('W') || down(VK_UP)) view_.origin.y += speed;
    if (down('S') || down(VK_DOWN)) view_.origin.y -= speed;
}

LRESULT GameApplication::onMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CLOSE: DestroyWindow(window_); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) { SetCursor(cursor_.handle(GetDpiForWindow(window_), cursorKind())); return TRUE; }
        break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT paint{}; BeginPaint(window_, &paint); EndPaint(window_, &paint); return 0; }
    case WM_SIZE: renderer_.resize(LOWORD(lParam), HIWORD(lParam)); return 0;
    case WM_DPICHANGED: {
        const auto* area = reinterpret_cast<RECT*>(lParam);
        fitToMonitor(MonitorFromRect(area, MONITOR_DEFAULTTONEAREST));
        return 0;
    }
    case WM_DISPLAYCHANGE:
        if (!IsIconic(window_)) fitToMonitor(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST));
        return 0;
    case WM_ACTIVATEAPP: {
        if (wParam && !IsIconic(window_)) fitToMonitor(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST));
        if (!wParam && GetCapture() == window_) ReleaseCapture();
        return 0;
    }
    case WM_MOUSEMOVE: {
        mouse_ = mousePosition(lParam);
        if (panning_) { view_.origin = view_.origin + mouse_ - panStart_; panStart_ = mouse_; }
        if (minimapDragging_) if (const auto c = rts::BattleLayout(renderer_.size()).minimapCell(mouse_, game_.map())) focus(rts::center(*c));
        if (dragging_ && (std::abs(mouse_.x - dragStart_.x) > 5 || std::abs(mouse_.y - dragStart_.y) > 5))
            ui_.drag = rts::UiRect{std::min(mouse_.x, dragStart_.x), std::min(mouse_.y, dragStart_.y),
                std::abs(mouse_.x - dragStart_.x), std::abs(mouse_.y - dragStart_.y)};
        refreshCursor();
        return 0;
    }
    case WM_CAPTURECHANGED:
        dragging_ = false; panning_ = false; minimapDragging_ = false; ui_.drag.reset(); return 0;
    case WM_MBUTTONDOWN:
        if (menu_.page == rts::MenuPage::Playing) { panning_ = true; panStart_ = mousePosition(lParam); SetCapture(window_); }
        return 0;
    case WM_MBUTTONUP: panning_ = false; ReleaseCapture(); return 0;
    case WM_LBUTTONDOWN: {
        mouse_ = mousePosition(lParam);
        if (menu_.page != rts::MenuPage::Playing) { menuClick(); return 0; }
        const rts::BattleLayout layout(renderer_.size());
        if (layout.menu.contains(mouse_)) { menu_.page = rts::MenuPage::Main; return 0; }
        if (layout.army.contains(mouse_)) { selectArmy(); return 0; }
        if (layout.hero.contains(mouse_)) { selectHero(); return 0; }
        if (layout.minimap.contains(mouse_)) {
            const auto c = layout.minimapCell(mouse_, game_.map());
            if (!c) return 0;
            if (ui_.rallyMode && selectedBuilding()) { if (game_.setRally(selectedBuilding()->id, *c)) ui_.rallyMode = false; }
            else { focus(rts::center(*c)); minimapDragging_ = true; SetCapture(window_); }
            return 0;
        }
        for (size_t i = 0; i < layout.commands.size(); ++i) if (layout.commands[i].contains(mouse_)) { action(i); return 0; }
        if (!mouseInWorld()) return 0;
        if (!ui_.placement.empty()) {
            if (const auto c = game_.map().pick(mouse_, view_))
                if (game_.construct(ui_.selection.ids, ui_.placement, *c)) ui_.placement.clear();
            return 0;
        }
        if (ui_.rallyMode) {
            if (const auto c = game_.map().pick(mouse_, view_)) if (const auto* b = selectedBuilding())
                if (game_.setRally(b->id, *c)) ui_.rallyMode = false;
            return 0;
        }
        dragging_ = true; adding_ = (wParam & MK_SHIFT) != 0; dragStart_ = mouse_; SetCapture(window_);
        return 0;
    }
    case WM_LBUTTONUP: {
        mouse_ = mousePosition(lParam);
        if (dragging_) {
            if (ui_.drag) ui_.selection.box(game_, view_, *ui_.drag, adding_);
            else ui_.selection.click(game_, view_, mouse_, adding_);
        }
        dragging_ = false; minimapDragging_ = false; ui_.drag.reset(); ReleaseCapture();
        return 0;
    }
    case WM_RBUTTONDOWN: {
        mouse_ = mousePosition(lParam);
        if (menu_.page != rts::MenuPage::Playing) return 0;
        const rts::BattleLayout layout(renderer_.size());
        if (layout.minimap.contains(mouse_)) rightClick(layout.minimapCell(mouse_, game_.map()));
        else if (mouseInWorld()) rightClick(selectedBuilding() ? game_.map().pick(mouse_, view_) : pickCommandTarget());
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (menu_.page != rts::MenuPage::Playing) {
            if (menu_.commanderDropdown) {
                const int offset = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
                const int last = std::max(0, static_cast<int>(definitions_.commanders().size()) - 5);
                menu_.commanderScroll = static_cast<size_t>(std::clamp(static_cast<int>(menu_.commanderScroll) + offset, 0, last));
            }
            return 0;
        }
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window_, &p);
        const float dpi = 96.0f / GetDpiForWindow(window_);
        mouse_ = {p.x * dpi, p.y * dpi};
        if (!mouseInWorld()) return 0;
        const float oldZoom = view_.zoom;
        view_.zoom = std::clamp(oldZoom * std::pow(1.15f, GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f), 0.35f, 2.0f);
        view_.origin = mouse_ - (mouse_ - view_.origin) * (view_.zoom / oldZoom);
        return 0;
    }
    case WM_SYSKEYDOWN:
        if (wParam == VK_F10 && forgeTest_) { testExitRequested_=true; DestroyWindow(window_); return 0; }
        break;
    case WM_KEYDOWN:
        if (lParam & (1LL << 30)) return 0;
        if (wParam == VK_F10 && forgeTest_) { testExitRequested_=true; DestroyWindow(window_); return 0; }
        if (wParam == VK_ESCAPE) {
            if (menu_.page == rts::MenuPage::Playing) {
                if (!ui_.placement.empty() || ui_.rallyMode) { ui_.placement.clear(); ui_.rallyMode = false; }
                else menu_.page = rts::MenuPage::Main;
            }
            else if (menu_.commanderDropdown) menu_.commanderDropdown = false;
            else if (menu_.page == rts::MenuPage::Main && menu_.canResume) menu_.page = rts::MenuPage::Playing;
            else menu_.page = rts::MenuPage::Main;
            return 0;
        }
        if (menu_.page != rts::MenuPage::Playing) return 0;
        if (wParam == VK_SPACE) paused_ = !paused_;
        if (wParam == 'G') grid_ = !grid_;
        if (wParam == VK_HOME) resetCamera();
        if (wParam == VK_F1) selectArmy();
        if (wParam == VK_F2) selectHero();
        if (wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) action(3);
        if (wParam == 'Q' && selectedBuilding()) action(0);
        if (wParam == 'R' && selectedBuilding()) action(1);
        if (wParam == 'E' && selectedBuilding()) action(2);
        if (wParam == 'T' && selectedBuilding()) action(4);
        if (wParam == 'H' && !selectedBuilding()) action(0);
        if (wParam == 'B' && !selectedBuilding()) action(1);
        if (wParam == 'O' && !selectedBuilding()) action(2);
        if (wParam == 'X') action(5);
        return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}
}
