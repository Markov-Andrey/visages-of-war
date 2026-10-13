#include "GameApplication.hpp"

namespace rts::game {
void GameApplication::clearCommandMode() {
    ui_.placement.clear(); ui_.rallyMode = false; ui_.orderMode.reset(); ui_.buildMenu = false; ui_.productionMenu = false;
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
    if (index == rts::abilityPageSlot && !ui_.buildMenu && !ui_.productionMenu) { ++ui_.abilityPage; return; }
    if (index == rts::productionMenuSlot && !ui_.buildMenu && !ui_.productionMenu) { clearCommandMode(); ui_.productionMenu = true; return; }
    if (ui_.buildMenu || ui_.productionMenu) {
        auto& page = ui_.buildMenu ? ui_.buildPage : ui_.productionPage;
        if (index == rts::nextPageSlot || index == rts::previousPageSlot) {
            const auto count = rts::commandPageCount(game_, ui_);
            if (count) page = (page % count + (index == rts::nextPageSlot ? 1 : count - 1)) % count;
            return;
        }
        if (const auto* type = rts::catalogCommandAt(game_, ui_, index)) {
            if (ui_.buildMenu) { ui_.placement = type->id; ui_.buildMenu = false; }
            else game_.train(rts::activeAbilityEntity(game_, ui_)->id, type->id);
        } else if (ui_.productionMenu && index == 1) ui_.rallyMode = true;
        else if (ui_.productionMenu && index == 5) game_.cancelTraining(rts::activeAbilityEntity(game_, ui_)->id);
        else if (index == rts::backCommandSlot) clearCommandMode();
        return;
    }
    if (const auto* ability = rts::abilityCommandAt(game_, ui_, index)) {
        const auto* entity = rts::activeAbilityEntity(game_, ui_);
        clearCommandMode();
        if (ability->targeting == AbilityTargeting::None) {
            for (auto id : rts::abilityRecipients(game_, ui_)) game_.activateAbility(id, ability->id);
        } else {
            ui_.orderMode = OrderKind::UseAbility; ui_.pendingAbility = ability->id;
            ui_.commandGroup = entity->definitionId;
        }
        return;
    }
    const auto* found = rts::unitCommandAt(index);
    if (!found) return;
    const auto& command = *found;
    if (command.command == rts::UnitCommand::Back) { clearCommandMode(); return; }
    if (!rts::commandEnabled(game_, ui_, command.command)) return;
    clearCommandMode();
    if (command.scope == rts::CommandScope::ActiveGroup) ui_.commandGroup = rts::activeAbilityEntity(game_, ui_)->definitionId;
    if (command.command == rts::UnitCommand::Build) ui_.buildMenu = true;
    else if (command.order == rts::OrderKind::Stop) game_.stop(ui_.selection.ids);
    else if (command.order == rts::OrderKind::Hold) game_.order(ui_.selection.ids, command.order);
    else ui_.orderMode = command.order;
}
bool GameApplication::unitHotkey(unsigned key) {
    if (selectedCrystal()) return false;
    if (key == 'X') if (const auto* e = selectedStructureOrProducer(); e && !e->complete()) { action(5); return true; }
    if ((key == 'N' || key == 'J') && (ui_.buildMenu || ui_.productionMenu)) {
        unitAction(key == 'N' ? rts::nextPageSlot : rts::previousPageSlot); return true;
    }
    if (ui_.buildMenu || ui_.productionMenu) {
        constexpr std::array buildKeys{L'H', L'B', L'O'};
        constexpr std::array trainKeys{L'Q', L'E', L'T'};
        for (size_t i = 0; i < 3; ++i) if (key == (ui_.buildMenu ? buildKeys[i] : trainKeys[i])) {
            unitAction(ui_.buildMenu ? i : i * 2); return true;
        }
        if (key == 'R' && ui_.productionMenu) { unitAction(1); return true; }
        if (key == 'D' && ui_.productionMenu) { unitAction(5); return true; }
        if (key == 'X') { clearCommandMode(); return true; }
        return false;
    }
    if (key == 'F') { unitAction(rts::abilityPageSlot); return true; }
    if (key == 'Y') { unitAction(rts::productionMenuSlot); return true; }
    for (size_t i = 0; i < rts::abilityKeys.size(); ++i)
        if (key == rts::abilityKey(game_, ui_, i) && rts::abilityCommandAt(game_, ui_, rts::firstAbilitySlot + i)) {
            unitAction(rts::firstAbilitySlot + i); return true;
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
