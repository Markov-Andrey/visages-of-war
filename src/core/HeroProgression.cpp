#include "CombatRules.hpp"
#include <algorithm>

namespace rts {
using combat::hostile;
bool Simulation::grantExperience(EntityId id, int amount) {
    auto* u = mutableUnit(id);
    if (!u || !u->hero || u->health <= 0 || amount <= 0 || u->atMaxLevel()) return false;
    const auto& thresholds = progression_->thresholds;
    const int oldMaximum = u->maximumHealth();
    u->hero->experience += std::min(amount, thresholds.back() - u->hero->experience);
    while (!u->atMaxLevel() && u->hero->experience >= thresholds[u->level()]) {
        ++u->hero->level;
        events_.emplace_back(HeroLevelChanged{u->id, u->level()});
    }
    u->health += u->maximumHealth() - oldMaximum; // Preserve damage already sustained.
    return true;
}
void Simulation::rewardKill(const Unit& victim, PlayerId killer) {
    if (!hostile(killer, victim.owner)) return;
    std::vector<EntityId> recipients;
    for (const auto& u : units_) if (u.hero && u.health > 0 && u.owner == killer && !u.atMaxLevel()) {
        const auto delta = u.position - victim.position;
        const int radius = progression_->experienceRadius;
        if (delta.x * delta.x + delta.y * delta.y <= radius * radius) recipients.push_back(u.id);
    }
    if (recipients.empty()) return;
    std::sort(recipients.begin(), recipients.end());
    const int bounty = progression_->experiencePerVictimLevel * victim.level();
    for (size_t i = 0; i < recipients.size(); ++i)
        grantExperience(recipients[i], bounty / static_cast<int>(recipients.size()) + (i < bounty % recipients.size() ? 1 : 0));
}
}
