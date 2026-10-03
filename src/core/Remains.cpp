#include "rts/UnitAnimation.hpp"
#include <algorithm>

namespace rts {
namespace {
std::uint32_t variation(EntityId id, std::uint64_t tick) {
    // Stable pseudo-random art choice, independent of rendering and unit type.
    auto value = id ^ static_cast<std::uint32_t>(tick) ^ static_cast<std::uint32_t>(tick >> 32);
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
}
const Corpse* Simulation::corpse(EntityId id) const {
    for (const auto& body : corpses_) if (body.id == id) return &body;
    return nullptr;
}
const Bones* Simulation::bones(EntityId id) const {
    for (const auto& pile : bones_) if (pile.id == id) return &pile;
    return nullptr;
}
void Simulation::leaveRemains(const Unit& unit) {
    Corpse body;
    body.id = nextId_++; body.sourceUnit = unit.id; body.definitionId = unit.definitionId;
    body.position = unit.position;
    body.cell = {int(std::floor(unit.position.x)), int(std::floor(unit.position.y))};
    body.height = map().surfaceHeight(body.cell, body.position);
    body.owner = unit.owner; body.sprite = unit.definition.sprite;
    body.facingRow = unitFrame(unit).row;
    if (!unit.definition.canDecompose) {
        body.phase = CorpsePhase::Vanishing;
        body.height = unitHeight(unit);
        if (body.sprite.death)
            body.vanishDuration = std::max(body.vanishDuration, int(body.sprite.death->frames.size()) * body.sprite.death->ticksPerFrame);
        body.remainingTicks = body.vanishDuration;
    } else events_.emplace_back(CorpseCreated{body.id, unit.id, unit.owner});
    corpses_.push_back(std::move(body));
}
void Simulation::tickRemains() {
    // Existing bones age first, so a newly created pile receives all 300 seconds.
    for (auto& pile : bones_) if (--pile.remainingTicks == 0) {
        if (!pile.sinking) { pile.sinking = true; pile.remainingTicks = Bones::sinkTicks; }
        else events_.emplace_back(RemainsRemoved{pile.id});
    }
    std::erase_if(bones_, [](const Bones& pile) { return pile.remainingTicks <= 0; });
    for (auto& body : corpses_) {
        ++body.elapsedTicks;
        if (--body.remainingTicks > 0) continue;
        if (body.phase == CorpsePhase::Body) {
            body.phase = CorpsePhase::Sinking; body.remainingTicks = Corpse::sinkTicks;
            continue;
        }
        if (body.phase == CorpsePhase::Sinking) {
            Bones pile;
            pile.id = nextId_++; pile.sourceUnit = body.sourceUnit; pile.sourceCorpse = body.id;
            pile.position = body.position; pile.cell = body.cell; pile.height = body.height;
            pile.variation = variation(pile.id, clock_.elapsedTicks());
            bones_.push_back(pile);
            events_.emplace_back(BonesCreated{pile.id, body.id});
        }
        if (body.phase != CorpsePhase::Vanishing) events_.emplace_back(RemainsRemoved{body.id});
    }
    std::erase_if(corpses_, [](const Corpse& body) { return body.remainingTicks <= 0; });
}
}
