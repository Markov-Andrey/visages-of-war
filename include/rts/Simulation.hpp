#pragma once
#include "rts/Navigation.hpp"
#include "rts/ecs/Systems.hpp"
#include "rts/FogOfWar.hpp"
#include "rts/WorldClock.hpp"
#include "rts/Projectile.hpp"
#include "rts/HealthFeedback.hpp"
#include "rts/UnitFacing.hpp"
#include "rts/MovementIndex.hpp"
#include "rts/AbilityAnimation.hpp"
#include <deque>
#include <string>
#include <variant>
#include <memory>

namespace rts {
enum class CorpsePhase { Body, Fading, Vanishing };
struct Corpse {
    static constexpr int lifetimeTicks = 60 * simulationTicksPerSecond;
    static constexpr int fadeTicks = 2 * simulationTicksPerSecond;
    static constexpr int vanishTicks = simulationTicksPerSecond;
    Vec2 position;
    Cell cell;
    float height;
    PlayerId owner;
    int remainingTicks = lifetimeTicks;
    UnitSpriteDefinition sprite;
    EntityId id{}, sourceUnit{};
    std::string definitionId;
    CorpsePhase phase = CorpsePhase::Body;
    int elapsedTicks{}, facingRow{}, vanishDuration = vanishTicks;
    int ageTicks() const { return elapsedTicks; }
};
struct Bones {
    static constexpr int lifetimeTicks = 300 * simulationTicksPerSecond;
    static constexpr int fadeTicks = 2 * simulationTicksPerSecond;
    EntityId id{}, sourceUnit{}, sourceCorpse{};
    Vec2 position;
    Cell cell;
    float height{};
    PlayerId owner = neutralPlayer;
    std::uint32_t variation{}; // Chosen once; renderer maps it to the catalog's variants.
    int remainingTicks = lifetimeTicks;
    bool fading{};
};
struct ConstructionFinished { EntityId building; };
struct UnitProduced { EntityId building; EntityId unit; };
struct UnitDied { EntityId unit; PlayerId owner; };
struct BuildingDestroyed { EntityId building; PlayerId owner; };
struct CorpseCreated { EntityId corpse, unit; PlayerId owner; };
struct BonesCreated { EntityId bones, corpse; };
struct RemainsRemoved { EntityId id; };
struct CrystalsHarvested {
    EntityId unit;
    PlayerId owner;
    Vec2 position;
    float height;
    int amount;
    std::uint64_t tick;
};

class Simulation {
public:
    static constexpr int ticksPerSecond = simulationTicksPerSecond;
    static constexpr int movementSearchesPerTick = 8;
    struct MovementStatistics { int searches{}, requests{}; size_t pending{}, candidates{}; };
    const MovementStatistics& movementStatistics() const { return movementStatistics_; }
    explicit Simulation(Scenario scenario, PlayerSettings player, EntityDefinition workerType,
        std::vector<EntityDefinition> entityTypes, std::string startingBuilding,
        std::string startingHero = {}, ProgressionRules progression = {}, ClockSettings clock = {}, ArmorRules armorRules = {});
    void addSystem(std::string name, ecs::Phase phase, ecs::Systems::Function function, int priority = 0) { systems_.add(std::move(name), phase, std::move(function), priority); }
    const ecs::World& world() const { return world_; }
    const ArmorRules& armorRules() const { return armorRules_; }
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    Simulation(Simulation&&) noexcept = default;
    Simulation& operator=(Simulation&&) noexcept = default;
    const PlayerSettings& player() const { return player_; }
    const EntityDefinition& workerType() const { return workerType_; }
    const PlayerSlots& playerSlots() const { return scenario_.slots; }
    bool activePlayer(PlayerId id) const { return id < maximumPlayers && scenario_.slots[id].active(); }
    const PlayerEconomy& economy(PlayerId id) const { return economies_.at(id); }
    const ArmySupply& armySupply(PlayerId id) const { return economy(id).supply; }
    const ArmySupply& armySupply() const { return armySupply(player_.id); }
    const WorldClock& clock() const { return clock_; }
    const FogOfWar& fog() const { return fog_; }
    void revealMap();
    const std::string& mapName() const { return scenario_.name; }
    const std::vector<EnvironmentObject>& environment() const { return scenario_.environment; }
    using WorldEvent = std::variant<DayPhaseChanged, ObjectDestroyed, ObjectRestored, ConstructionFinished, UnitProduced, UnitDied, BuildingDestroyed, HeroLevelChanged,
        CorpseCreated, BonesCreated, RemainsRemoved, CrystalsHarvested>;
    std::vector<WorldEvent> takeEvents();
    bool damageEnvironment(EntityId id, int damage);
    bool setEnvironmentHealth(EntityId id, int hitPoints);
    bool environmentVisible(size_t index) const;
    const Map& map() const { return scenario_.map; }
    ecs::EntityRange<const Entity> units() const { return world_.circles(); }
    const std::vector<Corpse>& corpses() const { return corpses_; }
    const std::vector<Bones>& bones() const { return bones_; }
    const Corpse* corpse(EntityId id) const;
    const Bones* bones(EntityId id) const;
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    const std::vector<ProjectileImpact>& projectileImpacts() const { return projectileImpacts_; }
    ecs::EntityRange<const Entity> buildings() const { return world_.footprints(); }
    std::vector<const EntityDefinition*> buildingTypes(const EntityDefinition& builder) const;
    std::vector<const Entity*> entities() const;
    const Entity* entity(EntityId id) const;
    const TagSet* tags(EntityId id) const;
    bool hasTag(EntityId id, std::string_view tag) const {
        const auto* set = tags(id); return set && set->contains(tag);
    }
    const Unit* unit(EntityId id) const;
    const Unit* hero() const;
    bool heroFallen() const { return heroFallen_; }
    bool grantExperience(EntityId id, int amount);
    const Building* building(EntityId id) const;
    const Building* buildingAt(Cell c) const;
    const EntityDefinition& entityType(const std::string& id) const;
    // Convenience accessors for the initial scenario; there is no singleton unit.
    const Unit& worker() const { return world_.circles().at(0); }
    Cell hall() const { return scenario_.hall; }
    const std::vector<Crystal>& crystals() const { return scenario_.crystals; }
    const Crystal* crystal(EntityId id) const;
    bool crystalVisible(const Crystal& node) const;
    int knownCrystal(size_t index) const {
        const auto& node = crystals().at(index);
        return crystalVisible(node) ? node.remaining : knownCrystals_.at(index);
    }
    bool knownEnvironment(size_t index) const { return knownEnvironment_.at(index); }
    const Landscape& landscape() const { return scenario_.landscape; }
    std::int64_t storedCrystals(PlayerId id) const { return economy(id).crystals; }
    std::int64_t storedCrystals() const { return storedCrystals(player_.id); }
    const std::wstring& message() const { return message_; }
    std::uint64_t messageRevision() const { return messageRevision_; }
    float unitHeight(const Unit& unit) const;
    float workerHeight() const { return unitHeight(worker()); }
    bool command(Cell c);
    bool command(std::span<const EntityId> ids, Cell c, EntityId target = 0);
    bool order(std::span<const EntityId> ids, OrderKind kind, Cell target = {});
    bool orderFor(PlayerId actor, std::span<const EntityId> ids, OrderKind kind, Cell target = {});
    bool attack(std::span<const EntityId> ids, EntityId target, bool forced = false);
    bool activateAbility(EntityId unit, int ability, EntityId target = 0);
    bool activateAbilityFor(PlayerId actor, EntityId unit, int ability, EntityId target = 0);
    bool setAutocast(EntityId unit, int ability, bool enabled);
    bool setAutocastFor(PlayerId actor, EntityId unit, int ability, bool enabled);
    bool canTargetAbility(const Unit& unit, const AbilityDefinition& ability, EntityId target) const;
    const AbilityDefinition* contextualAbility(const Unit& unit, EntityId target) const;
    bool canAttack(const Unit& attacker, const Unit& target) const;
    bool canAttack(const Unit& attacker, EntityId target, bool forced = false) const;
    void stop();
    void stop(std::span<const EntityId> ids);
    bool canPlace(const std::string& type, Cell origin) const;
    bool canPlaceFor(PlayerId actor, const std::string& type, Cell origin) const;
    std::optional<EntityId> construct(std::span<const EntityId> builders, const std::string& type, Cell origin);
    std::optional<EntityId> constructFor(PlayerId actor, std::span<const EntityId> builders, const std::string& type, Cell origin);
    bool cancelConstruction(EntityId id);
    bool cancelConstructionFor(PlayerId actor, EntityId id);
    bool train(EntityId buildingId, const std::string& definitionId = {});
    bool trainFor(PlayerId actor, EntityId buildingId, const std::string& definitionId = {});
    bool cancelTraining(EntityId buildingId, std::optional<size_t> queueIndex = std::nullopt);
    bool cancelTrainingFor(PlayerId actor, EntityId buildingId, std::optional<size_t> queueIndex = std::nullopt);
    bool setRally(EntityId buildingId, Cell cell);
    void tick();
private:
    void setMessage(std::wstring message) { message_ = std::move(message); ++messageRevision_; }
    Entity* mutableEntity(EntityId id);
    std::vector<Entity*> mutableEntities();
    Entity& makeEntity(const EntityDefinition& type, Cell cell, PlayerId owner);
    Unit* mutableUnit(EntityId id);
    Building* mutableBuilding(EntityId id);
    EntityId spawn(const EntityDefinition& definition, Cell cell, bool reserved, PlayerId owner);
    PlayerEconomy& mutableEconomy(PlayerId id) { return economies_.at(id); }
    bool visibleTo(PlayerId id, Cell cell) const;
    bool crystalVisibleTo(PlayerId id, const Crystal& crystal) const;
    std::vector<Cell> productionExits(const EntityDefinition& building, Cell origin, MovementType movement) const;
    Cell defaultRally(const EntityDefinition& building, Cell origin) const;
    std::vector<Cell> occupied(const Unit& unit, bool claimDestinations = false, bool ignoreGroup = false) const;
    std::vector<Circle> unitObstacles(const Unit& unit, bool claimDestinations = false, bool ignoreGroup = false) const;
    bool unitPositionFree(Vec2 position, const EntityDefinition& type, EntityId ignore = 0) const;
    bool dynamicStep(const Unit& unit, Vec2 to) const;
    void followPath(Unit& unit, Path path, UnitState state);
    void moveUnit(Unit& unit);
    bool groupArrived(const Unit& unit) const;
    void yieldFormation(Unit& unit);
    void prepareMovement();
    void replanMovement();
    void arbitrateTraffic();
    std::vector<size_t> movementCandidates(Vec2 from, Vec2 to, float radius) const;
    bool setRoute(Unit& unit, std::span<const Cell> goals, UnitState state, bool waitForTraffic = true);
    bool seekCrystal(Unit& unit);
    bool returnCargo(Unit& unit);
    void harvest(Entity& unit, const HarvestDefinition& effect, AbilityEffectState& state);
    bool startTargetedAbility(Unit& unit, const AbilityDefinition& ability, EntityId target, bool automatic);
    bool abilityEffectEligible(const Entity& caster, const AbilityDefinition& ability, EntityId target, bool automatic) const;
    bool useAbility(Unit& unit, const AbilityDefinition& ability, EntityId target, bool automatic = false);
    void commitAbility(Unit& unit, const AbilityDefinition& ability);
    std::vector<Cell> abilityPositions(const Entity& unit, const AbilityDefinition& ability, EntityId target) const;
    bool abilityReach(const Entity& unit, const AbilityDefinition& ability, EntityId target, Vec2 position) const;
    void approachAbility(Unit& unit);
    void tickAutocast(Unit& unit);
    void tickRepair(Entity& unit, const AbilityDefinition& ability, const RepairDefinition& repair, EntityId target, AbilityEffectState& state);
    bool effectEligible(const Entity& caster, const AbilityEffect& effect, EntityId target, bool automatic) const;
    void applyInstantEffects(Entity& caster, const AbilityDefinition& ability, EntityId target, std::vector<AbilityEffectState>& states);
    void tickAbility(Entity& unit);
    void runSystems(ecs::Phase phase);
    void arrived(Unit& unit);
    void issue(Unit& unit, Order order);
    void applyOrder(Unit& unit, Order order, std::optional<Path> path = {});
    void tickUnit(Unit& unit);
    bool constructionReach(const Entity& builder, const EntityDefinition& type, Cell origin) const;
    void advanceConstruction(Building& building);
    void tickConstruction();
    void tickProduction();
    void tickCombat();
    void leaveRemains(const Unit& unit);
    void tickRemains();
    struct CombatTarget {
        const Unit* unit{};
        const Building* building{};
        explicit operator bool() const { return unit || building; }
        const Entity& entity() const { return unit ? *unit : *building; }
        EntityId id() const { return entity().id; }
        PlayerId owner() const { return entity().owner; }
        Vital health() const { return entity().vitals().health; }
        bool air() const { return airborne(entity().definition.movement); }
        Vec2 position() const { return entity().position; }
    };
    CombatTarget combatTarget(EntityId id) const;
    bool canAttack(const Unit& attacker, CombatTarget target, bool forced) const;
    bool targetVisible(const Unit& observer, CombatTarget target) const;
    Vec2 attackAim(CombatTarget target, Vec2 from) const;
    bool attackReach(const Unit& attacker, CombatTarget target, std::optional<Vec2> origin = {}) const;
    void approachCombat(Unit& attacker, Vec2 aim, const CombatTarget* target);
    void cancelAttack(Unit& unit);
    void resumeOrder(Unit& unit);
    bool groundAttackReach(const Unit& unit, Cell target, std::optional<Cell> from = {}) const;
    void chaseGround(Unit& unit);
    struct Hit { EntityId target; int damage; PlayerId owner; };
    void releaseAttack(Unit& attacker, CombatTarget target, std::vector<Hit>& hits);
    void releaseGroundAttack(Unit& attacker);
    void releaseProjectile(Unit& attacker, Vec2 aim, float aimHeight, EntityId target);
    void tickProjectiles(std::vector<Hit>& hits);
    void resolveHits(const std::vector<Hit>& hits);
    void removeDeadEntities();
    void tickRegeneration();
    void rewardKill(const Unit& victim, PlayerId killer);
    void chase(Unit& attacker, CombatTarget target);
    void updateVision(bool force = true);
    Scenario scenario_;
    PlayerSettings player_;
    EntityDefinition workerType_;
    std::vector<EntityDefinition> entityTypes_;
    std::shared_ptr<const ProgressionRules> progression_;
    ArmorRules armorRules_;
    std::array<PlayerEconomy, maximumPlayers> economies_;
    WorldClock clock_;
    FogOfWar fog_;
    std::vector<VisionSource> visionSources_;
    std::vector<int> knownCrystals_;
    std::vector<bool> knownEnvironment_;
    std::vector<WorldEvent> events_;
    ecs::World world_;
    ecs::Systems systems_;
    std::vector<Corpse> corpses_;
    std::vector<Bones> bones_;
    std::vector<Projectile> projectiles_;
    std::vector<ProjectileImpact> projectileImpacts_;
    EntityId nextId_ = 10000;
    uint64_t nextMoveGroup_ = 1;
    int combatSearchBudget_ = 8;
    MovementIndex movementIndex_;
    bool movementIndexActive_{};
    std::deque<EntityId> movementReplans_;
    mutable MovementStatistics movementStatistics_;
    bool heroFallen_{};
    std::uint64_t messageRevision_{};
    std::wstring message_;
};
}
