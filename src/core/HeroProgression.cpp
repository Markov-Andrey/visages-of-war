#include "CombatRules.hpp"
#include <algorithm>

namespace rts {
using combat::hostile;
bool Simulation::grantExperience(EntityId id, int amount) {
    auto* u = mutableEntity(id);
    if (!u || !u->progression().hero || u->vitals().health <= 0 || amount <= 0 || u->atMaxLevel()) return false;
    const auto& thresholds = progression_->thresholds;
    const auto oldMaximum = u->maximumHealth();
    u->progression().hero->experience += std::min(amount, thresholds.back() - u->progression().hero->experience);
    while (!u->atMaxLevel() && u->progression().hero->experience >= thresholds[u->level()]) {
        ++u->progression().hero->level;
        events_.emplace_back(HeroLevelChanged{u->id, u->level()});
    }
    u->vitals().health += u->maximumHealth() - oldMaximum; // Preserve damage already sustained.
    const auto before = oldMaximum.hundredths(), after = u->maximumHealth().hundredths();
    u->vitals().repairCredit = (u->vitals().repairCredit / before) * after + (u->vitals().repairCredit % before) * after / before;
    return true;
}
void Simulation::rewardKill(const Unit& victim, PlayerId killer) {
    if (!hostile(killer, victim.owner)) return;
    std::vector<EntityId> recipients;
    const auto consider = [&](const Entity& u) {
        if (!u.progression().hero || u.vitals().health <= 0 || u.owner != killer || u.atMaxLevel()) return;
        const auto delta = u.position - victim.position;
        const int radius = progression_->experienceRadius;
        if (groundLengthSquared(delta) <= radius * radius) recipients.push_back(u.id);
    };
    for (const auto& u : world_.circles()) consider(u);
    for (const auto& b : world_.footprints()) consider(b);
    if (recipients.empty()) return;
    std::sort(recipients.begin(), recipients.end());
    const int bounty = progression_->experiencePerVictimLevel * victim.level();
    for (size_t i = 0; i < recipients.size(); ++i)
        grantExperience(recipients[i], bounty / static_cast<int>(recipients.size()) + (i < bounty % recipients.size() ? 1 : 0));
}
}
