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
    paused_ = false;
    ui_ = {};
    ui_.selection.army(game_);
    resetCamera();
}

void GameApplication::selectArmy() {
    ui_.selection.army(game_); ui_.placement.clear(); ui_.rallyMode = false;
}

void GameApplication::selectHero() {
    if (!ui_.selection.hero(game_)) return;
    ui_.placement.clear(); ui_.rallyMode = false;
    focus(game_.hero()->position);
}

void GameApplication::menuClick() {
    const rts::MenuLayout layout(renderer_.size(), menu_.canResume);
    if (menu_.page == rts::MenuPage::Main) {
        if (menu_.canResume && layout.resume.contains(mouse_)) menu_.page = rts::MenuPage::Playing;
        if (layout.battles.contains(mouse_)) menu_.page = rts::MenuPage::BattleSetup;
        if (layout.exit.contains(mouse_)) SendMessageW(window_, WM_CLOSE, 0, 0);
        return;
    }
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
    for (size_t i = 0; i < layout.colors.size(); ++i)
        if (layout.colors[i].contains(mouse_)) menu_.player.color = static_cast<rts::TeamColor>(i);
    if (layout.start.contains(mouse_) && menu_.mapSelected) startBattle();
}

const rts::Building* GameApplication::selectedBuilding() const {
    return ui_.selection.ids.size() == 1 ? game_.building(ui_.selection.ids.front()) : nullptr;
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
    if (index == 3) { game_.stop(ui_.selection.ids); ui_.placement.clear(); return; }
    if (index >= 3 || index >= game_.buildingTypes().size()) return;
    const bool canBuild = std::any_of(ui_.selection.ids.begin(), ui_.selection.ids.end(), [&](rts::EntityId id) {
        const auto* u = game_.unit(id); return u && u->definition.canBuild;
    });
    if (canBuild) { ui_.placement = game_.buildingTypes()[index]->id; ui_.rallyMode = false; }
}

void GameApplication::rightClick(std::optional<rts::Cell> target) {
    if (!ui_.placement.empty() || ui_.rallyMode) { ui_.placement.clear(); ui_.rallyMode = false; return; }
    if (!target || selectedCrystal()) return;
    if (const auto* b = selectedBuilding()) game_.setRally(b->id, *target);
    else game_.command(ui_.selection.ids, *target);
}

std::optional<rts::Cell> GameApplication::pickCommandTarget() const {
    std::optional<rts::Cell> result;
    float nearestDepth = -1;
    for (const auto& u : game_.units()) {
        const float depth = rts::airborne(u.definition.movement) ? 1000.0f + u.position.y : u.position.y;
        if (u.owner != game_.player().id && game_.fog().visible(u.cell) &&
            rts::unitBounds(game_, u, view_).contains(mouse_) && depth >= nearestDepth) {
            nearestDepth = depth; result = u.cell;
        }
    }
    for (const auto& crystal : game_.crystals()) {
        if (crystal.remaining <= 0 || !game_.fog().visible(crystal.cell)) continue;
        if (rts::crystalBounds(game_, crystal, view_).contains(mouse_)) {
            const float depth = crystal.cell.y + .5f;
            if (depth >= nearestDepth) { nearestDepth = depth; result = crystal.cell; }
        }
    }
    if (const auto id = rts::pickEntity(game_, view_, mouse_)) if (const auto* b = game_.building(*id)) {
        if (rts::buildingDepth(*b) >= nearestDepth) result = b->origin;
    }
    return result ? result : game_.map().pick(mouse_, view_);
}
}
