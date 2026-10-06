#include "rts/GameplayUi.hpp"
#include <tuple>

namespace rts {
std::vector<SelectionGroup> Selection::groups(const Simulation& game) const {
    std::vector<const Unit*> units;
    for (const auto id : ids) if (const auto* unit = game.unit(id);
        unit && unit->owner == game.player().id && unit->health > 0) units.push_back(unit);
    const auto key = [](const Unit* unit) {
        return std::tuple{!unit->hero.has_value(), unit->definition.formationPriority, unit->definition.id, unit->id};
    };
    std::sort(units.begin(), units.end(), [&](const Unit* a, const Unit* b) { return key(a) < key(b); });
    units.erase(std::unique(units.begin(), units.end()), units.end());
    std::vector<SelectionGroup> result;
    for (const auto* unit : units) {
        if (result.empty() || result.back().type != unit->definition.id) result.push_back({unit->definition.id, {}});
        result.back().ids.push_back(unit->id);
    }
    return result;
}

SelectionGroup Selection::activeGroup(const Simulation& game) const {
    const auto choices = groups(game);
    for (const auto& group : choices) if (group.type == activeType_) return group;
    return choices.empty() ? SelectionGroup{} : choices.front();
}

const Unit* Selection::activeUnit(const Simulation& game) const {
    const auto group = activeGroup(game);
    return group.ids.empty() ? nullptr : game.unit(group.ids.front());
}
const Unit* Selection::inspectedUnit(const Simulation& game) const {
    if (ids.size() == 1) return selectableEntity(game, ids.front()) ? game.unit(ids.front()) : nullptr;
    return activeUnit(game);
}

bool Selection::cycleGroup(const Simulation& game, bool reverse) {
    const auto choices = groups(game);
    if (choices.size() < 2) return false;
    size_t current = 0;
    for (size_t i = 0; i < choices.size(); ++i) if (choices[i].type == activeType_) { current = i; break; }
    activeType_ = choices[(current + (reverse ? choices.size() - 1 : 1)) % choices.size()].type;
    return true;
}

bool Selection::activateGroup(const Simulation& game, EntityId member) {
    const auto* unit = game.unit(member);
    if (!contains(member) || !unit || unit->owner != game.player().id || unit->health <= 0) return false;
    if (activeGroup(game).type == unit->definition.id) return false;
    activeType_ = unit->definition.id;
    return true;
}

bool Selection::selectMember(const Simulation& game, EntityId member) {
    const auto* unit = game.unit(member);
    if (!contains(member) || !unit || unit->owner != game.player().id || !selectableEntity(game, member)) return false;
    ids = {member};
    activeType_.clear();
    return true;
}

SelectionCards::SelectionCards(const Simulation& game, const Selection& selection, UiRect info) {
    const auto groups = selection.groups(game);
    const auto active = selection.activeGroup(game);
    std::vector<EntityId> ordered;
    size_t activeStart = 0;
    for (const auto& group : groups) {
        if (group.type == active.type) activeStart = ordered.size();
        ordered.insert(ordered.end(), group.ids.begin(), group.ids.end());
    }
    total = ordered.size();
    // One unit uses live stats; the separate preview never acts as a group button.
    if (total < 2) return;
    info = SelectionPanelLayout(info).content;
    const size_t columns = static_cast<size_t>(std::max(1.0f, (info.width + 6) / 46));
    const size_t capacity = columns * 2;
    // Always bring the active type into view, including selections larger than the HUD.
    first = activeStart / capacity * capacity;
    const size_t visible = std::min(total - first, capacity);
    const size_t rows = (visible + columns - 1) / columns;
    const float top = info.y + (info.height - (rows * 62.0f - 10)) * .5f;
    for (size_t i = first; i < first + visible; ++i) {
        const bool highlighted = game.unit(ordered[i])->definition.id == active.type;
        const float side = highlighted ? 40.0f : 32.0f;
        const float inset = (40 - side) * .5f;
        const size_t slot = i - first;
        const size_t row = slot / columns;
        const size_t rowCount = std::min(columns, visible - row * columns);
        const float left = info.x + (info.width - (rowCount * 46.0f - 6)) * .5f;
        cards.push_back({ordered[i], {left + (slot % columns) * 46.0f + inset,
            top + row * 62.0f + inset, side, side}, highlighted});
    }
}
}
