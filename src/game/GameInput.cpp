#include "GameApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace rts::game {
rts::CursorKind GameApplication::cursorKind() const {
    using rts::CursorKind;
    if (ui_.consoleOpen) return CursorKind::Default;
    if (menu_.page == rts::MenuPage::Library) return libraryLinkAt(mouse_) ? CursorKind::Hand : CursorKind::Default;
    if (menu_.page != rts::MenuPage::Playing) {
        const rts::MenuLayout layout(renderer_.size(), menu_.canResume);
        if (menu_.page == rts::MenuPage::Main) {
            if (layout.battles.contains(mouse_) || layout.library.contains(mouse_) || layout.exit.contains(mouse_) || (menu_.canResume && layout.resume.contains(mouse_))) return CursorKind::Hand;
        } else {
            const rts::ColorSelectLayout colors(layout.color, renderer_.size().y);
            if (menu_.colorDropdown) return colors.field.contains(mouse_) || colors.pick(mouse_).has_value() ? CursorKind::Hand : CursorKind::Default;
            if (menu_.commanderDropdown) {
                const auto count = std::min(size_t{5}, definitions_.commanders().size() - menu_.commanderScroll);
                if (rts::UiRect{layout.commanderOption.x, layout.commanderOption.y, layout.commanderOption.width, count * 44.0f}.contains(mouse_)) return CursorKind::Hand;
            }
            if (layout.back.contains(mouse_) || layout.commander.contains(mouse_) || (menu_.mapSelected && layout.start.contains(mouse_))) return CursorKind::Hand;
            if (!menu_.commanderDropdown && layout.color.contains(mouse_)) return CursorKind::Hand;
        }
        return CursorKind::Default;
    }
    if (panning_) return CursorKind::Move;
    if (const auto edge = cameraEdgeDirection(); edge != rts::Vec2{}) return rts::scrollCursor(edge);
    const rts::BattleLayout layout(renderer_.size());
    const auto* building = selectedBuilding();
    for (size_t i = 0; i < rts::controlGroupCount; ++i)
        if (!ui_.controlGroups.members(i).empty() && layout.controlGroups[i].contains(mouse_)) return CursorKind::Hand;
    const auto selected = [&](auto predicate) {
        return std::any_of(ui_.selection.ids.begin(), ui_.selection.ids.end(), [&](rts::EntityId id) {
            const auto* u = game_.unit(id);
            return u && u->owner == game_.player().id && predicate(*u);
        });
    };
    if (layout.menu.contains(mouse_) || layout.army.contains(mouse_)) return CursorKind::Hand;
    if (layout.hero.contains(mouse_)) return game_.hero() ? CursorKind::Hand : CursorKind::Blocked;
    if (layout.idleWorker.contains(mouse_)) return std::any_of(game_.units().begin(), game_.units().end(),
        [&](const auto& unit) { return rts::idleWorker(game_, unit); }) ? CursorKind::Hand : CursorKind::Blocked;
    const bool minimap = layout.minimap.contains(mouse_);
    if (!mouseInWorld() && !minimap) {
        for (const auto& card : rts::SelectionCards(game_, ui_.selection, layout.info).cards)
            if (card.bounds.contains(mouse_) || card.healthBar().contains(mouse_) ||
                (game_.unit(card.id)->maximumMana() > 0 && card.manaBar().contains(mouse_))) return CursorKind::Hand;
        for (size_t i = 0; i < layout.commandCount; ++i) if (layout.commands[i].contains(mouse_)) {
            bool active = false;
            if (building) active = (building->complete() && ((i == 1 && !building->definition.trainableUnits.empty()) ||
                ((i == 0 || i == 2 || i == 4) && i / 2 < building->definition.trainableUnits.size()))) ||
                (i == 5 && (!building->complete() || !building->production.empty()));
            else active = rts::unitCommandVisible(game_, ui_, i);
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
    if (ui_.orderMode) {
        if (!target) return CursorKind::Blocked;
        switch (*ui_.orderMode) {
        case rts::OrderKind::AttackMove: case rts::OrderKind::AttackGround: return CursorKind::Target;
        case rts::OrderKind::Gather:
            return std::any_of(game_.crystals().begin(), game_.crystals().end(),
                [&](const auto& crystal) { return crystal.contains(*target) && crystal.remaining > 0 && game_.crystalVisible(crystal); }) ? CursorKind::Gather : CursorKind::Blocked;
        default: return CursorKind::Move;
        }
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
    for (const auto& crystal : game_.crystals()) if (crystal.contains(*target) && crystal.remaining > 0 && game_.crystalVisible(crystal)) {
        if (selected([](const rts::Unit& u) { return u.definition.carryCapacity > 0; })) return CursorKind::Gather;
    }
    if (const auto id = rts::pickEntity(game_, view_, mouse_)) {
        const auto* site = game_.building(*id);
        if (site && site->owner == game_.player().id && !site->complete() && selected([](const rts::Unit& u) { return u.definition.canBuild; })) return CursorKind::Build;
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
    return rts::mouseInBattleWorld(ui_, rts::BattleLayout(renderer_.size()), mouse_);
}

rts::Vec2 GameApplication::mousePosition(LPARAM lParam) const {
    const float scale = 96.0f / GetDpiForWindow(window_);
    return {GET_X_LPARAM(lParam) * scale, GET_Y_LPARAM(lParam) * scale};
}

LRESULT GameApplication::onMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    // The console owns input while open; clicking must not issue world orders.
    if (ui_.consoleOpen && (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
        message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK || message == WM_MBUTTONDOWN ||
        message == WM_MBUTTONDBLCLK || message == WM_MOUSEWHEEL)) return 0;
    switch (message) {
    case WM_CHAR:
        if (ui_.consoleOpen) consoleCharacter(static_cast<wchar_t>(wParam));
        return 0;
    case WM_CLOSE: DestroyWindow(window_); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) { SetCursor(cursor_.handle(GetDpiForWindow(window_), cursorKind())); return TRUE; }
        break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT paint{}; BeginPaint(window_, &paint); EndPaint(window_, &paint); return 0; }
    case WM_SIZE: camera_.stop(view_); renderer_.resize(LOWORD(lParam), HIWORD(lParam)); constrainCamera(); return 0;
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
        if (!wParam) { camera_.stop(view_); mouse_ = ui_.mouse = {-1, -1}; }
        if (!wParam && GetCapture() == window_) ReleaseCapture();
        return 0;
    }
    case WM_MOUSEMOVE: {
        mouse_ = mousePosition(lParam);
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window_, 0};
        TrackMouseEvent(&tracking);
        if (panning_) { view_.origin = view_.origin + mouse_ - panStart_; panStart_ = mouse_; constrainCamera(); }
        if (minimapDragging_) if (const auto c = rts::BattleLayout(renderer_.size()).minimapCell(mouse_, game_.map())) focus(rts::center(*c));
        if (dragging_ && (std::abs(mouse_.x - dragStart_.x) > 5 || std::abs(mouse_.y - dragStart_.y) > 5))
            ui_.drag = rts::UiRect{std::min(mouse_.x, dragStart_.x), std::min(mouse_.y, dragStart_.y),
                std::abs(mouse_.x - dragStart_.x), std::abs(mouse_.y - dragStart_.y)};
        refreshCursor();
        return 0;
    }
    case WM_MOUSELEAVE:
        if (GetCapture() != window_) mouse_ = ui_.mouse = {-1, -1};
        return 0;
    case WM_CAPTURECHANGED:
        dragging_ = false; panning_ = false; minimapDragging_ = false; ui_.drag.reset(); return 0;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        if (menu_.page == rts::MenuPage::Playing) {
            camera_.stop(view_); panning_ = true; panStart_ = mousePosition(lParam); SetCapture(window_);
        }
        return 0;
    case WM_MBUTTONUP: panning_ = false; ReleaseCapture(); return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        mouse_ = mousePosition(lParam);
        if (menu_.page != rts::MenuPage::Playing) { menuClick(); return 0; }
        const rts::BattleLayout layout(renderer_.size());
        if (layout.menu.contains(mouse_)) { menu_.page = rts::MenuPage::Main; return 0; }
        if (layout.army.contains(mouse_)) { selectArmy(); return 0; }
        if (layout.hero.contains(mouse_)) { selectHero(); return 0; }
        if (layout.idleWorker.contains(mouse_)) { selectIdleWorker(); return 0; }
        for (size_t i = 0; i < rts::controlGroupCount; ++i)
            if (!ui_.controlGroups.members(i).empty() && layout.controlGroups[i].contains(mouse_)) {
                selectControlGroup(i); return 0;
            }
        if (layout.minimap.contains(mouse_)) {
            const auto c = layout.minimapCell(mouse_, game_.map());
            if (!c) return 0;
            if (ui_.orderMode) executeTarget(*c);
            else if (ui_.rallyMode && selectedBuilding()) { if (game_.setRally(selectedBuilding()->id, *c)) ui_.rallyMode = false; }
            else { focus(rts::center(*c)); minimapDragging_ = true; SetCapture(window_); }
            return 0;
        }
        for (size_t i = 0; i < layout.commandCount; ++i) if (layout.commands[i].contains(mouse_)) { action(i); return 0; }
        for (const auto& card : rts::SelectionCards(game_, ui_.selection, layout.info).cards) if (card.bounds.contains(mouse_) || card.healthBar().contains(mouse_) ||
                (game_.unit(card.id)->maximumMana() > 0 && card.manaBar().contains(mouse_))) {
            if (ui_.selection.selectMember(game_, card.id)) clearCommandMode();
            return 0;
        }
        if (!mouseInWorld()) return 0;
        if (ui_.orderMode) {
            if (const auto target = pickCommandTarget()) executeTarget(*target);
            return 0;
        }
        if (!ui_.placement.empty()) {
            if (const auto c = game_.map().pick(mouse_, view_))
                if (game_.construct(rts::commandRecipients(game_, ui_, rts::OrderKind::Build), ui_.placement, *c)) clearCommandMode();
            return 0;
        }
        if (ui_.rallyMode) {
            if (const auto c = game_.map().pick(mouse_, view_)) if (const auto* b = selectedBuilding())
                if (game_.setRally(b->id, *c)) ui_.rallyMode = false;
            return 0;
        }
        camera_.stop(view_);
        if (message == WM_LBUTTONDBLCLK && ui_.selection.selectTypeInView(game_, view_, layout.world, mouse_, (wParam & MK_SHIFT) != 0)) {
            clearCommandMode();
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
            clearCommandMode();
        }
        dragging_ = false; minimapDragging_ = false; ui_.drag.reset(); ReleaseCapture();
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK: {
        mouse_ = mousePosition(lParam);
        if (menu_.page != rts::MenuPage::Playing) return 0;
        const rts::BattleLayout layout(renderer_.size());
        if (layout.minimap.contains(mouse_)) rightClick(layout.minimapCell(mouse_, game_.map()));
        else if (mouseInWorld()) rightClick(selectedBuilding() ? game_.map().pick(mouse_, view_) : pickCommandTarget());
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (menu_.page == rts::MenuPage::Library) {
            POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window_, &p);
            const float dpi = 96.0f / GetDpiForWindow(window_);
            mouse_ = {p.x * dpi, p.y * dpi};
            libraryScroll(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
            return 0;
        }
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
        camera_.zoom(view_, mouse_, GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f);
        return 0;
    }
    case WM_SYSKEYDOWN:
        if (wParam == VK_F10 && forgeTest_) { testExitRequested_=true; DestroyWindow(window_); return 0; }
        break;
    case WM_KEYDOWN:
        if (lParam & (1LL << 30)) return 0;
        if (wParam == VK_F10 && forgeTest_) { testExitRequested_=true; DestroyWindow(window_); return 0; }
        if (consoleKey(wParam)) return 0;
        if ((menu_.page == rts::MenuPage::Library || menu_.page == rts::MenuPage::BattleSetup) && colorSelectKey(wParam)) return 0;
        if (wParam == VK_ESCAPE) {
            if (menu_.page == rts::MenuPage::Playing) {
                if (!ui_.placement.empty() || ui_.rallyMode || ui_.orderMode || ui_.buildMenu) clearCommandMode();
                else menu_.page = rts::MenuPage::Main;
            }
            else if (menu_.commanderDropdown) menu_.commanderDropdown = false;
            else if (menu_.page == rts::MenuPage::Main && menu_.canResume) menu_.page = rts::MenuPage::Playing;
            else menu_.page = rts::MenuPage::Main;
            return 0;
        }
        if (menu_.page != rts::MenuPage::Playing) return 0;
        if ((wParam >= '0' && wParam <= '9') || (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)) {
            const auto digit = wParam >= VK_NUMPAD0 ? wParam - VK_NUMPAD0 : wParam - '0';
            const size_t slot = digit == 0 ? 9 : size_t(digit - 1);
            if (GetKeyState(VK_CONTROL) & 0x8000) ui_.controlGroups.bind(slot, game_, ui_.selection);
            else selectControlGroup(slot);
            return 0;
        }
        if (wParam == VK_SPACE) paused_ = !paused_;
        if (wParam == 'K') grid_ = !grid_;
        if (wParam == VK_HOME) resetCamera(false);
        if (wParam == VK_F1) selectArmy();
        if (wParam == VK_F2) selectHero();
        if (wParam == VK_F8) selectIdleWorker();
        if (wParam == VK_TAB) {
            if (ui_.selection.cycleGroup(game_, (GetKeyState(VK_SHIFT) & 0x8000) != 0)) clearCommandMode();
            return 0;
        }
        if (unitHotkey(static_cast<unsigned>(wParam))) return 0;
        if (wParam == 'Q' && selectedBuilding()) action(0);
        if (wParam == 'R' && selectedBuilding()) action(1);
        if (wParam == 'E' && selectedBuilding()) action(2);
        if (wParam == 'T' && selectedBuilding()) action(4);
        if (wParam == 'X' && selectedBuilding()) action(5);
        return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}
}
