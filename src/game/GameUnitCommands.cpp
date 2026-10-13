#include "GameApplication.hpp"

namespace rts::game {
void GameApplication::clearCommandMode() {
    ui_.placement.clear(); ui_.rallyMode = false; ui_.orderMode.reset(); ui_.buildMenu = false;
    ui_.commandGroup.clear(); ui_.pendingAbility = 0;
}
void GameApplication::selectControlGroup(size_t slot) {
    if (ui_.controlGroups.recall(slot, game_, ui_.selection)) clearCommandMode();
}
void GameApplication::selectIdleWorker() {
    if (!ui_.selection.idleWorker(game_, ui_.idleWorkerCursor)) return;
    ui_.idleWorkerCursor = ui_.selection.ids.front();
    clearCommandMode();
    focus(game_.unit(ui_.idleWorkerCursor)->position);
}
void GameApplication::unitAction(size_t index) {
    if (!rts::unitCommandVisible(game_, ui_, index)) return;
    if (const auto* ability = rts::abilityCommandAt(game_, ui_, index)) {
        const auto* unit = rts::activeAbilityEntity(game_, ui_);
        clearCommandMode();
        if (ability->targeting == AbilityTargeting::None) game_.activateAbility(unit->id, ability->id);
        else {
            ui_.orderMode = OrderKind::UseAbility; ui_.pendingAbility = ability->id;
            ui_.commandGroup = unit->definitionId;
        }
        return;
    }
    if (ui_.buildMenu) {
        if (index < 3 && index < game_.buildingTypes().size() && rts::commandEnabled(game_, ui_, rts::UnitCommand::Build)) {
            ui_.placement = game_.buildingTypes()[index]->id; ui_.buildMenu = false;
        } else if (index == rts::backCommandSlot) clearCommandMode();
        return;
    }
    const auto* found = rts::unitCommandAt(index);
    if (!found) return;
    const auto& command = *found;
    if (command.command == rts::UnitCommand::Back) { clearCommandMode(); return; }
    if (!rts::commandEnabled(game_, ui_, command.command)) return;
    clearCommandMode();
    if (command.scope == rts::CommandScope::ActiveGroup) ui_.commandGroup = ui_.selection.activeGroup(game_).type;
    if (command.command == rts::UnitCommand::Build) ui_.buildMenu = true;
    else if (command.order == rts::OrderKind::Stop) game_.stop(ui_.selection.ids);
    else if (command.order == rts::OrderKind::Hold) game_.order(ui_.selection.ids, command.order);
    else ui_.orderMode = command.order;
}
bool GameApplication::unitHotkey(unsigned key) {
    if (selectedCrystal()) return false;
    if (ui_.buildMenu) {
        constexpr std::array keys{L'H', L'B', L'O'};
        for (size_t i = 0; i < keys.size(); ++i) if (key == keys[i]) { unitAction(i); return true; }
        if (key == 'X') { clearCommandMode(); return true; }
        return false;
    }
    for (size_t i = 0; i < rts::abilityKeys.size(); ++i)
        if (key == rts::abilityKey(game_, ui_, i) && rts::abilityCommandAt(game_, ui_, rts::firstAbilitySlot + i)) {
            unitAction(rts::firstAbilitySlot + i); return true;
        }
    if (const auto* b = selectedBuilding()) {
        if (key == 'S' && !b->definition.abilities.empty()) { action(rts::buildingAbilityStopSlot); return true; }
        if (key == 'X' && ui_.orderMode) { clearCommandMode(); return true; }
        return false;
    }
    for (const auto& command : rts::unitCommands) if (key == command.key) {
        unitAction(command.slot); return true;
    }
    return false;
}
bool GameApplication::executeTarget(rts::Cell target) {
    if (!ui_.orderMode) return false;
    bool accepted = false;
    const auto recipients = rts::commandRecipients(game_, ui_, *ui_.orderMode);
    if (*ui_.orderMode == OrderKind::UseAbility) {
        const auto entity = pickCommandEntity(target);
        if (entity) for (auto id : recipients) accepted = game_.activateAbility(id, ui_.pendingAbility, entity) || accepted;
        if (accepted) { ui_.commandFeedback.confirm(entity); clearCommandMode(); }
        return accepted;
    }
    const auto picked = *ui_.orderMode == rts::OrderKind::AttackMove ? pickCommandEntity(target) : 0;
    const auto entity = game_.entity(picked) ? picked : 0;
    if (entity) {
        accepted = game_.attack(recipients, entity, true);
        if (accepted) ui_.commandFeedback.confirm(entity);
    } else accepted = game_.order(recipients, *ui_.orderMode, target);
    if (accepted) clearCommandMode();
    return accepted;
}
}
