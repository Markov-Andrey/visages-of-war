#include "rts/GameplayUi.hpp"

namespace rts {
void ControlGroups::bind(size_t slot, const Simulation& game, const Selection& selection) {
    if (slot >= groups_.size()) return;
    auto& members = groups_[slot];
    members.clear();
    // Save the entire selection, independent of the type currently active under Tab.
    // Selection::groups also filters dead, foreign and non-unit entities and removes duplicates.
    for (const auto& group : selection.groups(game))
        members.insert(members.end(), group.ids.begin(), group.ids.end());
}

void ControlGroups::prune(const Simulation& game) {
    for (auto& members : groups_) std::erase_if(members, [&](EntityId id) {
        const auto* unit = game.unit(id);
        return !unit || unit->health <= 0 || unit->owner != game.player().id;
    });
}

bool ControlGroups::recall(size_t slot, const Simulation& game, Selection& selection) {
    if (slot >= groups_.size()) return false;
    prune(game);
    if (groups_[slot].empty()) return false;
    selection = {};
    selection.ids = groups_[slot];
    return true;
}

bool ControlGroups::selected(size_t slot, const Selection& selection) const {
    return slot < groups_.size() && !groups_[slot].empty() &&
        std::is_permutation(groups_[slot].begin(), groups_[slot].end(), selection.ids.begin(), selection.ids.end());
}
}
