#pragma once
#include "rts/Simulation.hpp"
#include <algorithm>
#include <utility>

namespace rts::tests {
// Test-only detached ECS fixture; copying an Entity handle must never alias mutable state.
inline void copyEntityState(Entity& target, const Entity& source) {
    target.owner = source.owner; target.definition = source.definition; target.definitionId = source.definitionId;
    target.cell = source.cell; target.position = source.position; target.facing = source.facing;
    target.origin = source.origin; target.rally = source.rally;
    target.vitals() = source.vitals(); target.motion() = source.motion(); target.orders() = source.orders();
    target.casting() = source.casting(); target.gathering() = source.gathering(); target.combat() = source.combat();
    target.progression() = source.progression(); target.productionState() = source.productionState();
}
inline EntityDefinition fixtureWorker() {
    EntityDefinition worker;
    worker.abilities = {AbilityDefinition{3, "Test cargo harvesting", "Deliver crystals to a depot", 0, 0,
        {HarvestDefinition{HarvestCredit::Cargo, 10, 1}}}};
    worker.gatherAbility()->targeting = AbilityTargeting::Crystal;
    worker.gatherAbility()->targets.relations = {TargetRelation::Neutral};
    worker.gatherAbility()->rightClick = true;
    worker.gatherAbility()->animation = AbilityAnimation::Work;
    return worker;
}
// Synthetic balance for isolated mechanics tests, never used by the game or Forge.
inline BuildingSpriteDefinition fixtureBuildingSprite() {
    BuildingSpriteDefinition sprite;
    sprite.scale = .16f;
    BuildingSpriteStage stage;
    stage.image = "sprites/buildings/valeri/ratusha/aligned/ratusha-no-fire.png";
    stage.source = {0, 0, 1312, 1199};
    sprite.stages.push_back(std::move(stage));
    return sprite;
}
inline EntityDefinition fixtureDepot(const EntityDefinition& worker) {
    EntityDefinition depot;
    depot.id = "church.hall"; depot.displayName = "Test hall"; depot.factionId = worker.factionId;
    depot.collision = CollisionShape::Footprint; depot.mobile = false; depot.movementPerSecond = 0; depot.canBuild = false; depot.carryCapacity = 0;
    depot.constructible = true; depot.width = 3; depot.height = 2; depot.cost = {200, 0};
    depot.constructionTicks = 300; depot.dayVision = 11; depot.nightVision = 8; depot.maximumHealth = 1000;
    depot.acceptsCargo = true; depot.trainableUnits = {worker.id};
    depot.buildingSprite = fixtureBuildingSprite();
    return depot;
}
inline Simulation makeTestSimulation(Scenario scenario, PlayerSettings player = {}, EntityDefinition worker = fixtureWorker(),
    std::vector<EntityDefinition> types = {}, std::string hero = {}, ProgressionRules progression = {}, ClockSettings clock = {}, ArmorRules armorRules = {}) {
    const auto depot = std::find_if(types.begin(), types.end(), [&](const auto& type) {
        return type.constructible && type.acceptsCargo && type.factionId == worker.factionId;
    });
    const auto startingBuilding = depot == types.end() ? fixtureDepot(worker).id : depot->id;
    if (depot == types.end()) types.push_back(fixtureDepot(worker));
    // Test fixtures explicitly author the available construction choices.
    if (worker.canBuild && worker.buildableTypes.empty())
        for (const auto& type : types) if (type.constructible && type.factionId == worker.factionId) worker.buildableTypes.push_back(type.id);
    for (auto& type : types) if (type.id == worker.id) type = worker;
    return Simulation(std::move(scenario), std::move(player), std::move(worker), std::move(types),
        startingBuilding, std::move(hero), std::move(progression), clock, armorRules);
}
}
