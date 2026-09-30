#pragma once
#include "rts/Navigation.hpp"
#include "rts/FogOfWar.hpp"
#include "rts/WorldClock.hpp"
#include "rts/Projectile.hpp"
#include <deque>
#include <string>
#include <variant>
#include <memory>

namespace rts {
enum class EntityKind { Unit, Building };
enum class UnitState { Idle, Moving, ToCrystal, Harvesting, ToHall, ToBuild, Building, WaitingForCrystal, ToAttack, Attacking };
enum class OrderKind { Move, Interact, Stop, Build, Gather, Attack };
struct Order { OrderKind kind; Cell cell{}; EntityId target{}; uint64_t moveGroup{}; };
struct HeroProgression {
    int level = 1; int experience{};
    std::shared_ptr<const ProgressionRules> rules;
};
struct HeroLevelChanged { EntityId id; int level; };
struct Unit {
    EntityId id{};
    PlayerId owner{};
    std::string definitionId;
    EntityDefinition definition;
    Cell cell;
    Vec2 position;
    Cell facing{0, 1};
    float walkCycle{};
    uint64_t moveGroup{};
    Vec2 formationForward{0, 1};
    float groupSpeed{};
    UnitState state = UnitState::Idle;
    int health{}, cargo{};
    std::vector<Cell> route;
    size_t next{};
    float progress{};
    int harvestTicks{}, blockedTicks{};
    int targetCrystal = -1;
    int gatherOriginCrystal = -1; // The clicked deposit remains the anchor across retries and deliveries.
    EntityId targetBuilding{};
    bool repeatGather{};
    std::optional<Order> pendingOrder;
    EntityId targetUnit{};
    AttackPhase attackPhase = AttackPhase::Ready;
    int attackTicks{}, chaseTicks{};
    std::optional<HeroProgression> hero;
    int level() const { return hero ? hero->level : definition.level; }
    int maximumHealth() const { return definition.maximumHealth + (hero ? (level() - 1) * definition.hero->healthPerLevel : 0); }
    int attackDamage() const { return definition.attackDamage + (hero ? (level() - 1) * definition.hero->damagePerLevel : 0); }
    bool atMaxLevel() const { return hero && level() == static_cast<int>(hero->rules->thresholds.size()); }
    int experienceInLevel() const { return hero ? hero->experience - hero->rules->thresholds[level() - 1] : 0; }
    int experienceToLevel() const { return hero && !atMaxLevel() ? hero->rules->thresholds[level()] - hero->rules->thresholds[level() - 1] : 0; }
    float experienceFraction() const { return atMaxLevel() ? 1.0f : experienceToLevel() ? float(experienceInLevel()) / experienceToLevel() : 0.0f; }
};
struct Corpse { Vec2 position; Cell cell; float height; PlayerId owner; int remainingTicks = 90; UnitSpriteDefinition sprite; };
struct ProductionJob {
    std::string definitionId;
    int remainingTicks{}, totalTicks{}, paidCrystals{}, reservedSupply{};
};
struct Building {
    EntityId id{};
    PlayerId owner{};
    EntityDefinition definition;
    Cell origin;
    int health{}, constructionProgress{};
    Cell rally;
    std::deque<ProductionJob> production;
    bool complete() const { return constructionProgress >= definition.constructionTicks; }
    bool training() const { return complete() && !production.empty() && production.front().remainingTicks > 0; }
    bool contains(Cell c) const {
        return c.x >= origin.x && c.y >= origin.y && c.x < origin.x + definition.width && c.y < origin.y + definition.height;
    }
};
struct ConstructionFinished { EntityId building; };
struct UnitProduced { EntityId building; EntityId unit; };
struct UnitDied { EntityId unit; PlayerId owner; };

class Simulation {
public:
    static constexpr int ticksPerSecond = 30;
    explicit Simulation(Scenario scenario, PlayerSettings player = {}, EntityDefinition workerType = {},
        std::vector<EntityDefinition> entityTypes = {}, std::string startingHero = {}, ProgressionRules progression = {});
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    Simulation(Simulation&&) noexcept = default;
    Simulation& operator=(Simulation&&) noexcept = default;
    const PlayerSettings& player() const { return player_; }
    const EntityDefinition& workerType() const { return workerType_; }
    const ArmySupply& armySupply() const { return supply_; }
    const WorldClock& clock() const { return clock_; }
    const FogOfWar& fog() const { return fog_; }
    const std::vector<EnvironmentObject>& environment() const { return scenario_.environment; }
    using WorldEvent = std::variant<DayPhaseChanged, ObjectDestroyed, ObjectRestored, ConstructionFinished, UnitProduced, UnitDied, HeroLevelChanged>;
    std::vector<WorldEvent> takeEvents();
    bool damageEnvironment(EntityId id, int damage);
    bool setEnvironmentHealth(EntityId id, int hitPoints);
    bool environmentVisible(size_t index) const;
    const Map& map() const { return scenario_.map; }
    const std::vector<Unit>& units() const { return units_; }
    const std::vector<Corpse>& corpses() const { return corpses_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    const std::vector<ProjectileImpact>& projectileImpacts() const { return projectileImpacts_; }
    const std::vector<Building>& buildings() const { return buildings_; }
    const std::vector<const EntityDefinition*>& buildingTypes() const { return buildable_; }
    const Unit* unit(EntityId id) const;
    const Unit* hero() const;
    bool heroFallen() const { return heroFallen_; }
    bool grantExperience(EntityId id, int amount);
    const Building* building(EntityId id) const;
    const Building* buildingAt(Cell c) const;
    const EntityDefinition& entityType(const std::string& id) const;
    // Convenience accessors for the initial scenario; there is no singleton unit.
    const Unit& worker() const { return units_.at(0); }
    Cell hall() const { return scenario_.hall; }
    const std::vector<Crystal>& crystals() const { return scenario_.crystals; }
    const Crystal* crystal(EntityId id) const;
    int knownCrystal(size_t index) const {
        const auto& node = crystals().at(index);
        return fog_.visible(node.cell) ? node.remaining : knownCrystals_.at(index);
    }
    bool knownEnvironment(size_t index) const { return knownEnvironment_.at(index); }
    const Landscape& landscape() const { return scenario_.landscape; }
    int storedCrystals() const { return stored_; }
    const std::wstring& message() const { return message_; }
    float unitHeight(const Unit& unit) const;
    float workerHeight() const { return unitHeight(worker()); }
    bool command(Cell c);
    bool command(std::span<const EntityId> ids, Cell c);
    bool attack(std::span<const EntityId> ids, EntityId target);
    bool canAttack(const Unit& attacker, const Unit& target) const;
    void stop();
    void stop(std::span<const EntityId> ids);
    bool canPlace(const std::string& type, Cell origin) const;
    std::optional<EntityId> construct(std::span<const EntityId> builders, const std::string& type, Cell origin);
    bool cancelConstruction(EntityId id);
    bool train(EntityId buildingId, const std::string& definitionId = {});
    bool cancelTraining(EntityId buildingId);
    bool setRally(EntityId buildingId, Cell cell);
    void tick();
private:
    Unit* mutableUnit(EntityId id);
    Building* mutableBuilding(EntityId id);
    EntityId spawn(const EntityDefinition& definition, Cell cell, bool reserved, PlayerId owner = 0);
    std::vector<Cell> occupied(const Unit& unit, bool claimDestinations = false, bool ignoreGroup = false) const;
    bool dynamicStep(const Unit& unit, Cell to) const;
    bool setRoute(Unit& unit, std::span<const Cell> goals, UnitState state, bool waitForTraffic = true);
    bool seekCrystal(Unit& unit);
    bool returnCargo(Unit& unit);
    void arrived(Unit& unit);
    void issue(Unit& unit, Order order);
    void applyOrder(Unit& unit, Order order);
    void tickUnit(Unit& unit);
    void tickProduction();
    void tickCombat();
    bool targetVisible(const Unit& observer, const Unit& target) const;
    bool attackReach(const Unit& attacker, const Unit& target, std::optional<Cell> from = {}) const;
    void cancelAttack(Unit& unit);
    struct Hit { EntityId target; int damage; PlayerId owner; };
    void releaseAttack(Unit& attacker, const Unit& target, std::vector<Hit>& hits);
    void tickProjectiles(std::vector<Hit>& hits);
    void resolveHits(const std::vector<Hit>& hits);
    void rewardKill(const Unit& victim, PlayerId killer);
    void chase(Unit& attacker, const Unit& target);
    void updateVision();
    Scenario scenario_;
    PlayerSettings player_;
    EntityDefinition workerType_;
    std::vector<EntityDefinition> entityTypes_;
    std::vector<const EntityDefinition*> buildable_;
    std::shared_ptr<const ProgressionRules> progression_;
    ArmySupply supply_;
    WorldClock clock_;
    FogOfWar fog_;
    std::vector<int> knownCrystals_;
    std::vector<bool> knownEnvironment_;
    std::vector<WorldEvent> events_;
    std::vector<Unit> units_;
    std::vector<Corpse> corpses_;
    std::vector<Projectile> projectiles_;
    std::vector<ProjectileImpact> projectileImpacts_;
    std::vector<Building> buildings_;
    EntityId nextId_ = 10000;
    uint64_t nextMoveGroup_ = 1;
    int stored_{};
    bool heroFallen_{};
    std::wstring message_ = L"Выберите юнитов рамкой или здание щелчком. ПКМ — приказ.";
};
}
