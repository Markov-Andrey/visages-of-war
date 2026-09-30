#include "DefinitionData.hpp"
#include "rts/Paths.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace rts {
namespace {
using namespace data;
Json read(const std::filesystem::path& file) {
    try {
        std::ifstream input(file);
        if (!input) throw std::runtime_error("Cannot open catalog file");
        std::vector<std::set<std::string>> keys;
        return Json::parse(input, [&](int, Json::parse_event_t event, Json& value) {
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
                throw std::runtime_error("Duplicate JSON key: " + value.get<std::string>());
            if (event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
    } catch (const std::exception& error) {
        const auto name = file.u8string();
        throw std::runtime_error(std::string(name.begin(), name.end()) + ": " + error.what());
    }
}
void version(const Json& root) { if (number(root.at("version"), 1, 1) != 1) throw std::runtime_error("Unsupported catalog version"); }
EntityDefinition parseEntity(const Json& j) {
    fields(j, {"id", "factionId", "name", "description", "cost", "stats", "mobility", "worker", "construction", "production", "depot", "hero", "visual", "alternateForms", "attack", "sprite"}, {"buildingSprite", "library"});
    EntityDefinition e;
    e.id = string(j.at("id")); e.factionId = string(j.at("factionId"));
    e.displayName = string(j.at("name")); e.description = string(j.at("description"));
    if (j.contains("library")) e.libraryVisible = j.at("library").get<bool>();
    const auto& cost = j.at("cost"); fields(cost, {"crystals", "supply"});
    e.cost = {number(cost.at("crystals"), 0, 1000000), number(cost.at("supply"), 0, ArmySupply::maximum)};
    const auto& stats = j.at("stats"); fields(stats, {"health", "damage", "level", "dayVision", "nightVision"});
    e.maximumHealth = number(stats.at("health"), 1, 1000000);
    e.attackDamage = number(stats.at("damage"), 0, 1000000);
    data::parseWeapon(e, j.at("attack"), j.at("sprite"));
    e.level = number(stats.at("level"), 1, 100);
    e.dayVision = number(stats.at("dayVision"), 1, 32); e.nightVision = number(stats.at("nightVision"), 1, 32);
    const auto& motion = j.at("mobility"); fields(motion, {"enabled", "type", "speed", "formationPriority"});
    e.mobile = motion.at("enabled").get<bool>();
    e.movementPerSecond = motion.at("speed").get<float>();
    if (!std::isfinite(e.movementPerSecond) || e.movementPerSecond < 0 || e.movementPerSecond > 15 ||
        (e.mobile && e.movementPerSecond <= 0)) throw std::runtime_error("Invalid movement speed");
    e.formationPriority = number(motion.at("formationPriority"), 1, 1000);
    const auto movement = string(motion.at("type"));
    if (movement == "walking") e.movement = MovementType::Walking;
    else if (movement == "swimming") e.movement = MovementType::Swimming;
    else if (movement == "amphibious") e.movement = MovementType::Amphibious;
    else if (movement == "flying") e.movement = MovementType::Flying;
    else throw std::runtime_error("Unknown movement type: " + movement);
    const auto& worker = j.at("worker"); fields(worker, {"builder", "carryCapacity"});
    e.canBuild = worker.at("builder").get<bool>(); e.carryCapacity = number(worker.at("carryCapacity"), 0, 1000000);
    const auto& construction = j.at("construction"); fields(construction, {"enabled", "ticks", "footprint"});
    e.constructible = construction.at("enabled").get<bool>(); e.constructionTicks = number(construction.at("ticks"), 1, 1000000);
    const auto& footprint = construction.at("footprint");
    if (!footprint.is_array() || footprint.size() != 2) throw std::runtime_error("Footprint must contain width and height");
    e.width = number(footprint[0], 1, 8); e.height = number(footprint[1], 1, 8);
    const auto& production = j.at("production"); fields(production, {"trainingTicks", "trains"});
    e.trainingTicks = number(production.at("trainingTicks"), 1, 1000000);
    const auto& trains = production.at("trains");
    if (!trains.is_array() || trains.size() > 3) throw std::runtime_error("Production supports up to three unit types");
    std::set<std::string> choices;
    for (const auto& value : trains) {
        auto id = string(value);
        if (!choices.insert(id).second) throw std::runtime_error("Duplicate production choice");
        e.trainableUnits.push_back(std::move(id));
    }
    e.acceptsCargo = j.at("depot").get<bool>();
    const auto& hero = j.at("hero");
    if (!hero.is_null()) {
        fields(hero, {"healthPerLevel", "damagePerLevel"});
        if (e.level != 1) throw std::runtime_error("Hero must start at level one");
        e.hero = HeroDefinition{number(hero.at("healthPerLevel"), 0, 100000), number(hero.at("damagePerLevel"), 0, 100000)};
    }
    const auto visual = string(j.at("visual"));
    if (visual == "unit") e.visual = EntityVisual::Unit;
    else if (visual == "hall") e.visual = EntityVisual::Hall;
    else if (visual == "barracks") e.visual = EntityVisual::Barracks;
    else if (visual == "tower") e.visual = EntityVisual::Tower;
    else throw std::runtime_error("Unknown visual: " + visual);
    e.alternateForms = j.at("alternateForms").get<std::vector<std::string>>();
    if (j.contains("buildingSprite")) data::parseBuildingSprite(e, j.at("buildingSprite"));
    return e;
}
}
const EntityDefinition& Definitions::startingDepot(const std::string& factionId) const {
    const auto found = std::find_if(entities_.begin(), entities_.end(), [&](const auto& type) {
        return type.constructible && type.acceptsCargo && type.factionId == factionId;
    });
    if (found == entities_.end()) throw std::runtime_error("Faction requires a starting resource depot: " + factionId);
    return *found;
}
Definitions Definitions::load(const std::filesystem::path& catalog) {
    Definitions result;
    const auto catalogPath = std::filesystem::absolute(catalog);
    const Paths paths(catalogPath.parent_path(), catalogPath.parent_path());
    const auto resolve = [&](const Json& name) {
        const auto text = string(name);
        return paths.asset(std::filesystem::path(std::u8string(text.begin(), text.end())));
    };
    const auto root = read(catalogPath);
    fields(root, {"version", "factions", "entityFiles", "commandersFile", "rulesFile"}); version(root);
    if (!root.at("factions").is_array() || root.at("factions").empty()) throw std::runtime_error("No factions");
    for (const auto& j : root.at("factions")) {
        fields(j, {"id", "name", "description"});
        FactionDefinition faction{string(j.at("id")), string(j.at("name")), string(j.at("description"))};
        for (const auto& previous : result.factions_) if (previous.id == faction.id) throw std::runtime_error("Duplicate faction: " + faction.id);
        result.factions_.push_back(std::move(faction));
    }
    const auto rules = read(resolve(root.at("rulesFile"))); fields(rules, {"version", "hero"}); version(rules);
    const auto& hero = rules.at("hero"); fields(hero, {"experienceRadius", "experiencePerVictimLevel", "thresholds"});
    result.progression_.experienceRadius = number(hero.at("experienceRadius"), 1, 64);
    result.progression_.experiencePerVictimLevel = number(hero.at("experiencePerVictimLevel"), 0, 1000000);
    const auto& thresholds = hero.at("thresholds");
    if (!thresholds.is_array() || thresholds.size() < 2 || thresholds.size() > 100) throw std::runtime_error("Invalid hero level count");
    result.progression_.thresholds.clear();
    for (const auto& t : thresholds) {
        const auto value = number(t, 0, 100000000);
        if (result.progression_.thresholds.empty() ? value != 0 : value <= result.progression_.thresholds.back())
            throw std::runtime_error("Hero thresholds must increase from zero");
        result.progression_.thresholds.push_back(value);
    }
    if (!root.at("entityFiles").is_array() || root.at("entityFiles").empty()) throw std::runtime_error("No entity files");
    for (const auto& name : root.at("entityFiles")) {
        const auto file = resolve(name);
        const auto content = read(file); fields(content, {"version", "entities"}); version(content);
        if (!content.at("entities").is_array()) throw std::runtime_error("Expected entity array");
        for (const auto& j : content.at("entities")) {
            try {
                auto entity = parseEntity(j);
                entity.factionName = result.faction(entity.factionId).displayName;
                if (!result.entityIndex_.emplace(entity.id, result.entities_.size()).second) throw std::runtime_error("Duplicate entity ID");
                result.entities_.push_back(std::move(entity));
            } catch (const std::exception& error) {
                const auto fileName = file.u8string();
                throw std::runtime_error(std::string(fileName.begin(), fileName.end()) + " / " + j.value("id", std::string("?")) + ": " + error.what());
            }
        }
    }
    if (result.entities_.empty()) throw std::runtime_error("No entities");
    for (const auto& e : result.entities_) {
        for (const auto& id : e.trainableUnits) {
            const auto& unit = result.entity(id);
            if (!unit.mobile || unit.width != 1 || unit.height != 1) throw std::runtime_error("Production needs a mobile one-cell unit: " + id);
        }
        std::set<std::string> forms;
        for (const auto& id : e.alternateForms) {
            result.entity(id);
            if (id == e.id || !forms.insert(id).second) throw std::runtime_error("Invalid alternate form: " + id);
        }
    }
    const auto commanders = read(resolve(root.at("commandersFile"))); fields(commanders, {"version", "commanders"}); version(commanders);
    if (!commanders.at("commanders").is_array()) throw std::runtime_error("Expected commander array");
    for (const auto& j : commanders.at("commanders")) {
        fields(j, {"id", "name", "description", "factionId", "startingWorker", "startingHero"});
        CommanderDefinition c;
        c.id = string(j.at("id")); c.displayName = string(j.at("name")); c.description = string(j.at("description"));
        c.factionId = string(j.at("factionId")); c.factionName = result.faction(c.factionId).displayName;
        c.startingWorker = string(j.at("startingWorker")); c.startingHero = string(j.at("startingHero"), true);
        const auto& worker = result.entity(c.startingWorker);
        if (!worker.mobile || !worker.isWorker() || worker.factionId != c.factionId) throw std::runtime_error("Invalid starting worker");
        if (!c.startingHero.empty()) {
            const auto& type = result.entity(c.startingHero);
            if (!type.hero || !type.mobile || type.factionId != c.factionId) throw std::runtime_error("Invalid starting hero");
        }
        for (const auto& previous : result.commanders_) if (previous.id == c.id) throw std::runtime_error("Duplicate commander: " + c.id);
        result.commanders_.push_back(std::move(c));
    }
    if (result.commanders_.empty()) throw std::runtime_error("No commanders");
    return result;
}
const EntityDefinition& Definitions::entity(const std::string& id) const {
    const auto found = entityIndex_.find(id);
    if (found == entityIndex_.end()) throw std::out_of_range("Unknown entity: " + id);
    return entities_.at(found->second);
}
const CommanderDefinition& Definitions::commander(const std::string& id) const {
    for (const auto& c : commanders_) if (c.id == id) return c;
    throw std::out_of_range("Unknown commander: " + id);
}
const FactionDefinition& Definitions::faction(const std::string& id) const {
    for (const auto& f : factions_) if (f.id == id) return f;
    throw std::out_of_range("Unknown faction: " + id);
}
}
