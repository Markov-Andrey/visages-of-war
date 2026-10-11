#pragma once
#include "rts/Simulation.hpp"
#include <algorithm>
#include <utility>

namespace rts::tests {
// Synthetic balance for isolated mechanics tests, never used by the game or Forge.
inline EntityDefinition fixtureDepot(const EntityDefinition& worker) {
    EntityDefinition depot;
    depot.id = "human.hall"; depot.displayName = "Test hall"; depot.factionId = worker.factionId;
    depot.mobile = false; depot.movementPerSecond = 0; depot.canBuild = false; depot.carryCapacity = 0;
    depot.constructible = true; depot.width = 3; depot.height = 2; depot.cost = {200, 0};
    depot.constructionTicks = 300; depot.dayVision = 11; depot.nightVision = 8; depot.maximumHealth = 1000;
    depot.acceptsCargo = true; depot.trainableUnits = {worker.id}; depot.visual = EntityVisual::Hall;
    return depot;
}
inline Simulation makeTestSimulation(Scenario scenario, PlayerSettings player = {}, EntityDefinition worker = {},
    std::vector<EntityDefinition> types = {}, std::string hero = {}, ProgressionRules progression = {}, ClockSettings clock = {}) {
    const auto depot = std::find_if(types.begin(), types.end(), [&](const auto& type) {
        return type.constructible && type.acceptsCargo && type.factionId == worker.factionId;
    });
    const auto startingBuilding = depot == types.end() ? fixtureDepot(worker).id : depot->id;
    if (depot == types.end()) types.push_back(fixtureDepot(worker));
    return Simulation(std::move(scenario), std::move(player), std::move(worker), std::move(types),
        startingBuilding, std::move(hero), std::move(progression), clock);
}
}
