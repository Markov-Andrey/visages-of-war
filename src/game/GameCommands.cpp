#include "GameApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace rts::game {
void GameApplication::startBattle() {
    const auto& commander = definitions_.commanders().at(menu_.commanderIndex);
    menu_.player.commanderId = commander.id;
    game_ = makeMatch();
    menu_.page = rts::MenuPage::Playing;
    menu_.canResume = true;
    menu_.commanderDropdown = false;
    menu_.colorDropdown = false;
    paused_ = false;
    ui_ = {};
    ui_.selection.army(game_);
    resetCamera();
}

void GameApplication::selectArmy() {
    ui_.selection.army(game_); clearCommandMode();
}

void GameApplication::selectHero() {
    if (!ui_.selection.hero(game_)) return;
    clearCommandMode();
    focus(game_.hero()->position);
}

void GameApplication::menuClick() {
    if (menu_.page == rts::MenuPage::Library) { libraryClick(); return; }
    const rts::MenuLayout layout(renderer_.size(), menu_.canResume);
    if (menu_.page == rts::MenuPage::Main) {
        if (menu_.canResume && layout.resume.contains(mouse_)) menu_.page = rts::MenuPage::Playing;
        if (layout.battles.contains(mouse_)) { menu_.page = rts::MenuPage::BattleSetup; menu_.colorDropdown = false; }
        if (layout.library.contains(mouse_)) { menu_.page = rts::MenuPage::Library; menu_.librarySeconds = 0; menu_.colorDropdown = false; }
        if (layout.exit.contains(mouse_)) SendMessageW(window_, WM_CLOSE, 0, 0);
        return;
    }
    if (colorSelectClick(rts::ColorSelectLayout(layout.color, renderer_.size().y), mouse_)) return;
    if (menu_.commanderDropdown) {
        const size_t count = std::min(size_t{5}, definitions_.commanders().size() - menu_.commanderScroll);
        for (size_t i = 0; i < count; ++i) {
            const rts::UiRect option{layout.commanderOption.x, layout.commanderOption.y + i * 44.0f, layout.commanderOption.width, 44};
            if (option.contains(mouse_)) {
                menu_.commanderIndex = menu_.commanderScroll + i;
                menu_.commanderDropdown = false;
                return;
            }
        }
        menu_.commanderDropdown = false;
        return;
    }
    if (layout.back.contains(mouse_)) menu_.page = rts::MenuPage::Main;
    if (layout.commander.contains(mouse_)) menu_.commanderDropdown = true;
    if (layout.start.contains(mouse_) && menu_.mapSelected) startBattle();
}

bool GameApplication::colorSelectClick(const rts::ColorSelectLayout& layout, rts::Vec2 mouse) {
    if (menu_.colorDropdown) {
        if (const auto choice = layout.pick(mouse)) {
            menu_.colorFocus = *choice;
            menu_.player.color = static_cast<rts::TeamColor>(*choice);
        }
        menu_.colorDropdown = false;
        return true; // Closing a popup must not activate the control underneath.
    }
    if (!menu_.commanderDropdown && layout.field.contains(mouse)) {
        menu_.colorDropdown = true;
        menu_.colorFocus = static_cast<size_t>(menu_.player.color);
        return true;
    }
    return false;
}

bool GameApplication::colorSelectKey(WPARAM key) {
    if (!menu_.colorDropdown) return false;
    const int count = static_cast<int>(rts::teamPalettes.size());
    int offset = 0;
    switch (key) {
    case VK_ESCAPE: menu_.colorDropdown = false; return true;
    case VK_RETURN:
        menu_.player.color = static_cast<rts::TeamColor>(menu_.colorFocus);
        menu_.colorDropdown = false; return true;
    case VK_HOME: menu_.colorFocus = 0; return true;
    case VK_END: menu_.colorFocus = rts::teamPalettes.size() - 1; return true;
    case VK_LEFT: offset = -1; break;
    case VK_RIGHT: offset = 1; break;
    case VK_UP:
    case VK_DOWN: {
        const int columns = static_cast<int>(rts::ColorSelectLayout::columns);
        const int column = static_cast<int>(menu_.colorFocus) % columns;
        const int rows = (count - 1 - column) / columns + 1;
        const int row = (static_cast<int>(menu_.colorFocus) / columns + (key == VK_UP ? -1 : 1) + rows) % rows;
        menu_.colorFocus = static_cast<size_t>(row * columns + column);
        return true;
    }
    default: return false;
    }
    menu_.colorFocus = static_cast<size_t>((static_cast<int>(menu_.colorFocus) + offset + count) % count);
    return true;
}

const rts::Building* GameApplication::selectedBuilding() const {
    const auto* building = ui_.selection.ids.size() == 1 ? game_.building(ui_.selection.ids.front()) : nullptr;
    return building && building->owner == game_.player().id && rts::selectableEntity(game_, building->id) ? building : nullptr;
}

const rts::Crystal* GameApplication::selectedCrystal() const {
    return ui_.selection.ids.size() == 1 ? game_.crystal(ui_.selection.ids.front()) : nullptr;
}

void GameApplication::action(size_t index) {
    if (selectedCrystal()) return;
    if (const auto* b = selectedBuilding()) {
        const auto id = b->id;
        if (index == 0 || index == 2 || index == 4) {
            const auto choice = index / 2;
            if (choice < b->definition.trainableUnits.size()) game_.train(id, b->definition.trainableUnits[choice]);
        }
        if (index == 1 && !b->definition.trainableUnits.empty()) { ui_.rallyMode = true; ui_.placement.clear(); }
        if (index == 5) { if (!b->complete()) game_.cancelConstruction(id); else game_.cancelTraining(id); }
        ui_.selection.prune(game_);
        return;
    }
    unitAction(index);
}

void GameApplication::rightClick(std::optional<rts::Cell> target) {
    if (!ui_.placement.empty() || ui_.rallyMode || ui_.orderMode || ui_.buildMenu) { clearCommandMode(); return; }
    if (!target || selectedCrystal()) return;
    if (const auto* b = selectedBuilding()) game_.setRally(b->id, *target);
    else if (ui_.selection.activeUnit(game_) && game_.command(ui_.selection.ids, *target)) {
        // Match Simulation::command's target precedence. In a mixed selection,
        // ineligible members retain old orders and must not confirm their old target.
        for (const auto& unit : game_.units()) if (unit.owner != game_.player().id && unit.cell == *target && game_.fog().visible(unit.cell)) {
            ui_.commandFeedback.confirm(unit.id); return;
        }
        for (const auto& node : game_.crystals()) if (node.contains(*target) && node.remaining > 0 && game_.crystalVisible(node)) {
            ui_.commandFeedback.confirm(node.id); return;
        }
    }
}

std::optional<rts::Cell> GameApplication::pickCommandTarget() const {
    std::optional<rts::Cell> result;
    float nearestDepth = -1;
    for (const auto& u : game_.units()) {
        const float depth = rts::airborne(u.definition.movement) ? 2000.0f + rts::unitDrawDepth(u.position) : rts::unitDrawDepth(u.position);
        if (u.owner != game_.player().id && game_.fog().visible(u.cell) &&
            rts::unitBounds(game_, u, view_).contains(mouse_) && depth >= nearestDepth) {
            nearestDepth = depth; result = u.cell;
        }
    }
    for (const auto& crystal : game_.crystals()) {
        if (crystal.remaining <= 0 || !game_.crystalVisible(crystal)) continue;
        if (rts::crystalBounds(game_, crystal, view_).contains(mouse_)) {
            const float depth = crystal.depth();
            if (depth >= nearestDepth) { nearestDepth = depth; result = crystal.cell; }
        }
    }
    if (const auto id = rts::pickEntity(game_, view_, mouse_)) if (const auto* b = game_.building(*id)) {
        if (rts::buildingDepth(*b) >= nearestDepth) result = b->origin;
    }
    return result ? result : game_.map().pick(mouse_, view_);
}
}
