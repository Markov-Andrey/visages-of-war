#include "GameApplication.hpp"

namespace rts::game {
void GameApplication::clearCommandMode() {
    ui_.placement.clear(); ui_.rallyMode = false; ui_.orderMode.reset(); ui_.buildMenu = false;
    ui_.commandGroup.clear();
}
void GameApplication::selectIdleWorker() {
    if (!ui_.selection.idleWorker(game_, ui_.idleWorkerCursor)) return;
    ui_.idleWorkerCursor = ui_.selection.ids.front();
    clearCommandMode();
    focus(game_.unit(ui_.idleWorkerCursor)->position);
}
void GameApplication::unitAction(size_t index) {
    if (!rts::unitCommandVisible(game_, ui_, index)) return;
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
    if (selectedBuilding() || selectedCrystal()) return false;
    if (ui_.buildMenu) {
        constexpr std::array keys{L'H', L'B', L'O'};
        for (size_t i = 0; i < keys.size(); ++i) if (key == keys[i]) { unitAction(i); return true; }
        if (key == 'X') { clearCommandMode(); return true; }
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
    bool enemy = false;
    const auto recipients = rts::commandRecipients(game_, ui_, *ui_.orderMode);
    if (*ui_.orderMode == rts::OrderKind::AttackMove) for (const auto& unit : game_.units())
        if (unit.cell == target && unit.owner != game_.player().id && unit.owner != rts::neutralPlayer && game_.fog().visible(unit.cell)) {
            enemy = true; accepted = game_.attack(recipients, unit.id); break;
        }
    if (!enemy) accepted = game_.order(recipients, *ui_.orderMode, target);
    if (accepted) clearCommandMode();
    return accepted;
}
}
