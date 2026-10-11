#pragma once
#include "rts/Definitions.hpp"
#include "rts/Simulation.hpp"
#include <map>
#include <set>
#include <span>

namespace rts {
struct FactionAssets {
    std::set<unsigned> colors;
    std::set<std::string> entities;
    bool operator==(const FactionAssets&) const = default;
};
// A match owns only the graphic blocks it can use. Catalog metadata stays shared.
struct MatchAssets {
    std::map<std::string, FactionAssets> factions;
    std::set<std::string> materials, objects, crystals;
    bool operator==(const MatchAssets&) const = default;
};
// Additional types let a future mission declare reinforcements before they spawn.
MatchAssets collectMatchAssets(const Simulation& game, const Definitions& definitions,
    std::span<const std::string> additionalEntities = {});
}
