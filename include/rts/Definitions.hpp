#pragma once
#include "rts/Match.hpp"
#include <filesystem>
#include <unordered_map>

namespace rts {
struct FactionDefinition { std::string id, displayName, description; };
struct CommanderDefinition {
    std::string id, displayName, description;
    std::string factionId, factionName;
    std::string startingWorker, startingHero;
};
class Definitions {
public:
    static Definitions load(const std::filesystem::path& catalog);
    const std::vector<CommanderDefinition>& commanders() const { return commanders_; }
    const CommanderDefinition& commander(const std::string& id) const;
    const FactionDefinition& faction(const std::string& id) const;
    const EntityDefinition& entity(const std::string& id) const;
    const std::vector<EntityDefinition>& entities() const { return entities_; }
    const ProgressionRules& progression() const { return progression_; }
private:
    std::vector<EntityDefinition> entities_;
    std::unordered_map<std::string, size_t> entityIndex_;
    std::vector<CommanderDefinition> commanders_;
    std::vector<FactionDefinition> factions_;
    ProgressionRules progression_;
};
}
