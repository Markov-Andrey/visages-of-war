#include "rts/MatchAssets.hpp"

namespace rts {
MatchAssets collectMatchAssets(const Simulation& game, const Definitions& definitions,
    std::span<const std::string> additionalEntities) {
    MatchAssets result;
    const unsigned local = teamRgb(game.player().color);
    const unsigned enemy = teamRgb(game.player().color == TeamColor::Red ? TeamColor::Blue : TeamColor::Red);
    const auto add = [&](const EntityDefinition& type, unsigned color) {
        result.factions[type.factionId].colors.insert(color);
    };
    add(game.workerType(), local);
    for (const auto& unit : game.units()) add(unit.definition, unit.owner == game.player().id ? local : enemy);
    for (const auto& building : game.buildings()) add(building.definition, building.owner == game.player().id ? local : enemy);
    for (const auto& id : additionalEntities) {
        add(definitions.entity(id), local);
        add(definitions.entity(id), enemy);
    }
    // Include future production/building types, and cross-faction transformations
    // or production references, rather than just entities placed at tick zero.
    bool changed;
    do {
        changed = false;
        for (const auto& type : definitions.entities()) {
            const auto found = result.factions.find(type.factionId);
            if (found == result.factions.end()) continue;
            found->second.entities.insert(type.id);
            for (const auto& ids : {type.trainableUnits, type.alternateForms}) for (const auto& id : ids) {
                auto& destination = result.factions[definitions.entity(id).factionId];
                for (unsigned color : found->second.colors) changed |= destination.colors.insert(color).second;
            }
        }
    } while (changed);
    result.materials.insert(game.landscape().baseMaterial);
    for (const auto& paint : game.landscape().paint) result.materials.insert(paint.material);
    for (const auto& object : game.environment()) {
        if (!object.definitionId.empty()) result.objects.insert(object.definitionId);
        else if (object.kind == EnvironmentKind::Tree) result.objects.insert("tree");
    }
    for (const auto& decoration : game.landscape().decorations) result.objects.insert(decoration.definitionId);
    for (const auto& crystal : game.crystals()) result.crystals.insert(crystal.definitionId);
    return result;
}
}
