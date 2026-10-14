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
EntityDefinition parseEntity(const Json& j, const TagCatalog& tags) {
    fields(j, {"id", "factionId", "name", "description", "cost", "stats", "mobility", "worker", "construction", "production", "depot", "hero", "alternateForms", "attack", "sprite", "collision"}, {"buildingSprite", "canDecompose", "abilities", "tags"});
    EntityDefinition e;
    e.tags = tags.resolve(j.value("tags", std::vector<std::string>{}));
    e.id = string(j.at("id")); e.factionId = string(j.at("factionId"));
    e.displayName = string(j.at("name")); e.description = string(j.at("description"));
    if (j.contains("canDecompose")) e.canDecompose = j.at("canDecompose").get<bool>();
    const auto& cost = j.at("cost"); fields(cost, {"crystals", "supply"});
    e.cost = {number(cost.at("crystals"), 0, 1000000), number(cost.at("supply"), 0, ArmySupply::maximum)};
    const auto& stats = j.at("stats"); fields(stats, {"health", "armor", "damage", "level", "dayVision", "nightVision"}, {"mana"});
    e.maximumHealth = number(stats.at("health"), 1, 1000000);
    e.armor = number(stats.at("armor"), -1000000, 1000000);
    if (stats.contains("mana")) e.maximumMana = number(stats.at("mana"), 0, 1000000);
    e.attackDamage = number(stats.at("damage"), 0, 1000000);
    data::parseWeapon(e, j.at("attack"), j.at("sprite"));
    e.level = number(stats.at("level"), 1, 100);
    e.dayVision = number(stats.at("dayVision"), 1, 32); e.nightVision = number(stats.at("nightVision"), 1, 32);
    const auto& collision = j.at("collision"); fields(collision, {"shape", "radius", "footprint"});
    const auto shape = string(collision.at("shape"));
    if (shape == "circle") e.collision = CollisionShape::Circle;
    else if (shape == "footprint") e.collision = CollisionShape::Footprint;
    else throw std::runtime_error("Unknown collision shape");
    e.collisionRadius = real(collision.at("radius"), .05f, .5f);
    const auto& footprint = collision.at("footprint");
    if (!footprint.is_array() || footprint.size() != 2) throw std::runtime_error("Footprint must contain width and height");
    e.width = number(footprint[0], 1, 8); e.height = number(footprint[1], 1, 8);
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
    const auto& worker = j.at("worker"); fields(worker, {"builds", "carryCapacity"});
    e.buildableTypes = worker.at("builds").get<std::vector<std::string>>(); e.canBuild = !e.buildableTypes.empty(); e.carryCapacity = number(worker.at("carryCapacity"), 0, 1000000);
    const auto& construction = j.at("construction"); fields(construction, {"enabled", "ticks"}, {"mode"});
    e.constructible = construction.at("enabled").get<bool>(); e.constructionTicks = number(construction.at("ticks"), 1, 1000000);
    if (construction.contains("mode")) {
        const auto mode = string(construction.at("mode"));
        if (mode == "worker") e.constructionMode = ConstructionMode::Worker;
        else if (mode == "summoning") e.constructionMode = ConstructionMode::Summoning;
        else throw std::runtime_error("Unknown construction mode: " + mode);
    }
    const auto& production = j.at("production"); fields(production, {"trainingTicks", "trains"});
    e.trainingTicks = number(production.at("trainingTicks"), 1, 1000000);
    const auto& trains = production.at("trains");
    if (!trains.is_array()) throw std::runtime_error("Expected production array");
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
    e.alternateForms = j.at("alternateForms").get<std::vector<std::string>>();
    if (j.contains("buildingSprite")) data::parseBuildingSprite(e, j.at("buildingSprite"));
    if (e.constructible && e.buildingSprite.stages.empty()) throw std::runtime_error("Constructible entity requires buildingSprite stages");
    return e;
}
}
const EntityDefinition& Definitions::startingDepot(const std::string& factionId) const {
    const auto& id = faction(factionId).startingBuilding;
    if (id.empty()) throw std::runtime_error("Faction requires startingBuilding: " + factionId);
    const auto& type = entity(id);
    if (!type.constructible || type.mobile || !type.acceptsCargo || type.factionId != factionId)
        throw std::runtime_error("Invalid startingBuilding for faction " + factionId + ": " + id);
    return type;
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
    fields(root, {"version", "factions", "entityFiles", "commandersFile", "rulesFile", "abilitiesFile", "tagsFile"}); version(root);
    result.tags_ = TagCatalog::load(resolve(root.at("tagsFile")));
    if (!root.at("factions").is_array() || root.at("factions").empty()) throw std::runtime_error("No factions");
    for (const auto& j : root.at("factions")) {
        fields(j, {"id", "name", "description"}, {"startingBuilding"});
        FactionDefinition faction{string(j.at("id")), string(j.at("name")), string(j.at("description"))};
        if (j.contains("startingBuilding")) faction.startingBuilding = string(j.at("startingBuilding"));
        for (const auto& previous : result.factions_) if (previous.id == faction.id) throw std::runtime_error("Duplicate faction: " + faction.id);
        result.factions_.push_back(std::move(faction));
    }
    const auto abilities = read(resolve(root.at("abilitiesFile")));
    fields(abilities, {"version", "abilities"}); version(abilities);
    if (!abilities.at("abilities").is_array()) throw std::runtime_error("Expected ability array");
    for (const auto& j : abilities.at("abilities")) {
        fields(j, {"id", "name", "description", "targeting", "manaCost", "cooldownTicks", "effects"}, { "autocast", "rightClick", "animation", "targets", "range"});
        const auto targeting = string(j.at("targeting"));
        AbilityDefinition ability{number(j.at("id"), 1, 1000000), string(j.at("name")), string(j.at("description")),
            number(j.at("manaCost"), 0, 1000000), number(j.at("cooldownTicks"), 0, 1000000)};
        if (targeting == "none") ability.targeting = AbilityTargeting::None;
        else if (targeting == "crystal") ability.targeting = AbilityTargeting::Crystal;
        else if (targeting == "building") ability.targeting = AbilityTargeting::Building;
        else if (targeting == "unit") ability.targeting = AbilityTargeting::Unit;
        else if (targeting == "entity") ability.targeting = AbilityTargeting::Entity;
        else throw std::runtime_error("Unknown ability targeting");
        if (j.contains("targets")) {
            const auto& t = j.at("targets"); fields(t, {"relations", "damagedOnly", "completedOnly", "allowSelf"});
            if (!t.at("relations").is_array() || t.at("relations").empty()) throw std::runtime_error("Expected target relations");
            ability.targets.relations.clear();
            for (const auto& value : t.at("relations")) {
                const auto relation = string(value);
                if (relation == "own") ability.targets.relations.push_back(TargetRelation::Own);
                else if (relation == "enemy") ability.targets.relations.push_back(TargetRelation::Enemy);
                else if (relation == "neutral") ability.targets.relations.push_back(TargetRelation::Neutral);
                else throw std::runtime_error("Unknown target relation");
            }
            ability.targets.damagedOnly = t.at("damagedOnly").get<bool>();
            ability.targets.completedOnly = t.at("completedOnly").get<bool>();
            ability.targets.allowSelf = t.at("allowSelf").get<bool>();
        }
        if (j.contains("range")) ability.range = real(j.at("range"), 0, 64);
        if (j.contains("rightClick")) ability.rightClick = j.at("rightClick").get<bool>();
        if (ability.rightClick && ability.targeting == AbilityTargeting::None)
            throw std::runtime_error("Right-click casting requires a target");
        if (j.contains("animation")) {
            const auto animation = string(j.at("animation"));
            if (animation == "work") ability.animation = AbilityAnimation::Work;
            else if (animation == "attack") ability.animation = AbilityAnimation::Attack;
            else if (animation == "stand") ability.animation = AbilityAnimation::Stand;
            else throw std::runtime_error("Unknown ability animation");
        }
        if (j.contains("autocast")) {
            const auto& a = j.at("autocast"); fields(a, {"radius", "scanIntervalTicks", "enabledByDefault", "condition"});
            if (string(a.at("condition")) != "stopped") throw std::runtime_error("Unknown autocast condition");
            ability.autocast = AutocastDefinition{real(a.at("radius"), .1f, 64), number(a.at("scanIntervalTicks"), 1, 3600),
                a.at("enabledByDefault").get<bool>(), AutocastCondition::Stopped};
        }
        if (!j.at("effects").is_array()) throw std::runtime_error("Expected effect array");
        for (const auto& effect : j.at("effects")) {
            const auto kind = string(effect.at("type"));
            const auto to = string(effect.at("recipient"));
            if (to != "target" && to != "caster") throw std::runtime_error("Unknown effect recipient");
            const auto recipient = to == "caster" ? EffectRecipient::Caster : EffectRecipient::Target;
            if (kind == "harvest") {
                fields(effect, {"type", "recipient", "credit", "intervalTicks", "amount"}, {"effect"});
                if (recipient != EffectRecipient::Target || ability.targeting != AbilityTargeting::Crystal)
                    throw std::runtime_error("Harvest requires a crystal target");
                const auto& h = effect;
            HarvestDefinition harvest;
            const auto credit = string(h.at("credit"));
            if (credit == "direct") harvest.credit = HarvestCredit::Direct;
            else if (credit != "cargo") throw std::runtime_error("Unknown harvest credit mode: " + credit);
            harvest.intervalTicks = number(h.at("intervalTicks"), 1, 1000000);
            harvest.amount = number(h.at("amount"), 1, 1000000);
            if (h.contains("effect")) {
                const auto& visual = h.at("effect"); fields(visual, {"color", "width", "arcHeight"});
                auto& visualEffect = harvest.effect.emplace();
                const auto& rgb = visual.at("color");
                if (!rgb.is_array() || rgb.size() != 3) throw std::runtime_error("Harvest color requires RGB components");
                visualEffect.color = (number(rgb[0], 0, 255) << 16) | (number(rgb[1], 0, 255) << 8) | number(rgb[2], 0, 255);
                visualEffect.width = real(visual.at("width"), .1f, 8);
                visualEffect.arcHeight = real(visual.at("arcHeight"), 0, 128);
            }
                ability.effects.emplace_back(harvest, recipient);
            } else if (kind == "repair" || kind == "restoreHealth" || kind == "restoreMana") {
                if (recipient == EffectRecipient::Target && (ability.targeting == AbilityTargeting::None || ability.targeting == AbilityTargeting::Crystal))
                    throw std::runtime_error("Entity effect requires an entity recipient");
                if (kind == "repair") {
                    fields(effect, {"type", "recipient", "constructionTimePercent", "constructionCostPercent"});
                    ability.effects.emplace_back(RepairDefinition{number(effect.at("constructionTimePercent"), 1, 1000),
                        number(effect.at("constructionCostPercent"), 0, 1000)}, recipient);
                } else {
                    fields(effect, {"type", "recipient", "amount"});
                    ability.effects.emplace_back(RestoreDefinition{kind == "restoreHealth" ? RestoredStat::Health : RestoredStat::Mana,
                        number(effect.at("amount"), 1, 1000000)}, recipient);
                }
            } else throw std::runtime_error("Unknown ability effect: " + kind);
        }
        for (const auto& previous : result.abilities_) if (previous.id == ability.id) throw std::runtime_error("Duplicate ability ID");
        result.abilities_.push_back(std::move(ability));
    }
    const auto rules = read(resolve(root.at("rulesFile"))); fields(rules, {"version", "hero", "armor"}); version(rules);
    const auto& armor = rules.at("armor"); fields(armor, {"coefficient"});
    if (!armor.at("coefficient").is_number()) throw std::runtime_error("Armor coefficient must be numeric");
    result.armorRules_.coefficient = armor.at("coefficient").get<double>();
    result.armorRules_.validate();
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
                auto entity = parseEntity(j, result.tags_);
                if (j.contains("abilities")) {
                    const auto& ids = j.at("abilities");
                    if (!ids.is_array()) throw std::runtime_error("Expected ability array");
                    std::set<int> unique;
                    for (const auto& value : ids) {
                        const int id = number(value, 1, 1000000);
                        if (!unique.insert(id).second) throw std::runtime_error("Duplicate entity ability");
                        const auto& ability = result.ability(id);
                        if (ability.usesCargo() && entity.carryCapacity == 0)
                            throw std::runtime_error("Cargo harvesting requires positive carryCapacity");
                        entity.abilities.push_back(ability);
                    }
                }
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
        std::set<std::string> builds;
        for (const auto& id : e.buildableTypes)
            if (!result.entity(id).constructible || !builds.insert(id).second) throw std::runtime_error("Invalid construction choice: " + id);
        for (const auto& id : e.trainableUnits) {
            const auto& unit = result.entity(id);
            if (unit.collision != CollisionShape::Circle || unit.width != 1 || unit.height != 1)
                throw std::runtime_error("Production currently needs a one-cell circle collider: " + id);
        }
        std::set<std::string> forms;
        for (const auto& id : e.alternateForms) {
            result.entity(id);
            if (id == e.id || !forms.insert(id).second) throw std::runtime_error("Invalid alternate form: " + id);
        }
    }
    for (const auto& faction : result.factions_)
        if (!faction.startingBuilding.empty()) result.startingDepot(faction.id);
    const auto commanders = read(resolve(root.at("commandersFile"))); fields(commanders, {"version", "commanders"}); version(commanders);
    if (!commanders.at("commanders").is_array()) throw std::runtime_error("Expected commander array");
    for (const auto& j : commanders.at("commanders")) {
        fields(j, {"id", "name", "description", "factionId", "startingWorker", "startingHero"}, {"rallySprite"});
        CommanderDefinition c;
        c.id = string(j.at("id")); c.displayName = string(j.at("name")); c.description = string(j.at("description"));
        c.factionId = string(j.at("factionId")); c.factionName = result.faction(c.factionId).displayName;
        result.startingDepot(c.factionId);
        c.startingWorker = string(j.at("startingWorker")); c.startingHero = string(j.at("startingHero"), true);
        if (j.contains("rallySprite") && !j.at("rallySprite").is_null()) c.rallySprite = data::parseRallySprite(j.at("rallySprite"));
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
const AbilityDefinition& Definitions::ability(int id) const {
    for (const auto& ability : abilities_) if (ability.id == id) return ability;
    throw std::out_of_range("Unknown ability: " + std::to_string(id));
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
