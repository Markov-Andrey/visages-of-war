#include "rts/Simulation.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace rts {
Simulation::Simulation(Scenario scenario, PlayerSettings player, EntityDefinition workerType,
    std::vector<EntityDefinition> entityTypes, std::string startingBuilding, std::string startingHero, ProgressionRules progression, ClockSettings clock)
    : scenario_(std::move(scenario)), player_(std::move(player)), workerType_(std::move(workerType)),
      entityTypes_(std::move(entityTypes)), progression_(std::make_shared<const ProgressionRules>(std::move(progression))), clock_(clock), fog_(map().width(), map().height()),
      knownCrystals_(crystals().size()), knownEnvironment_(environment().size()) {
    validatePlayerSlots(scenario_.slots);
    if (!activePlayer(player_.id) || scenario_.slots[player_.id].state != PlayerSlotState::Player)
        throw std::invalid_argument("Local player requires a player slot");
    for (size_t i = 0; i < maximumPlayers; ++i) economies_[i].crystals = scenario_.slots[i].startingCrystals;
    for (const auto& object : scenario_.environment) nextId_ = std::max(nextId_, object.id + 1);
    for (const auto& node : crystals())
        if (node.capacity < 1 || node.capacity > 1000000 || node.remaining < 0 || node.remaining > node.capacity ||
            node.width < 1 || node.width > 16 || node.height < 1 || node.height > 16 ||
            !map().contains(node.cell) || !map().contains(node.cell + Cell{node.width - 1, node.height - 1}))
            throw std::invalid_argument("Invalid crystal reserve or footprint");
    if (std::none_of(entityTypes_.begin(), entityTypes_.end(), [&](const auto& type) { return type.id == workerType_.id; })) entityTypes_.push_back(workerType_);
    for (const auto& type : entityTypes_) if (type.constructible && type.factionId == workerType_.factionId) buildable_.push_back(&type);
    const auto initial = std::find_if(buildable_.begin(), buildable_.end(), [&](const auto* type) { return type->id == startingBuilding; });
    if (initial == buildable_.end() || (*initial)->mobile || !(*initial)->acceptsCargo)
        throw std::invalid_argument("Invalid starting building for faction " + workerType_.factionId + ": " + startingBuilding);
    reserveScenarioHall(scenario_, {(*initial)->width, (*initial)->height});
    if (!mutableEconomy(player_.id).supply.reserve((*initial)->cost.supply)) throw std::invalid_argument("Initial depot exceeds supply cap");
    buildings_.push_back(makeEntity(**initial, hall(), player_.id));
    spawn(workerType_, scenario_.worker, false, player_.id);
    for (Cell cell : scenario_.extraWorkers) spawn(workerType_, cell, false, player_.id);
    for (const auto& initialUnit : scenario_.units) spawn(entityType(initialUnit.definitionId), initialUnit.cell, false, initialUnit.owner);
    if (!startingHero.empty()) {
        if (!scenario_.heroSpawn || !entityType(startingHero).hero) throw std::invalid_argument("Missing hero spawn or progression");
        spawn(entityType(startingHero), *scenario_.heroSpawn, false, player_.id);
    }
    for (auto& node : scenario_.crystals) node.id = nextId_++;
    scenario_.map.rebuildVisionBlockers(environment());
    updateVision();
}
bool Simulation::visibleTo(PlayerId id, Cell cell) const {
    if (!activePlayer(id)) return false;
    if (id == player_.id) return fog_.visible(cell);
    const bool day = clock_.phase() == DayPhase::Day;
    for (const auto& u : units_) if (u.owner == id && u.health > 0 &&
        visionReaches(map(), {u.cell, day ? u.definition.dayVision : u.definition.nightVision,
            airborne(u.definition.movement)}, cell)) return true;
    for (const auto& b : buildings_) if (b.owner == id && b.health > 0) {
        const int radius = b.complete() ? (day ? b.definition.dayVision : b.definition.nightVision) : 2;
        if (visionReaches(map(), {b.origin + Cell{b.definition.width / 2, b.definition.height / 2}, radius}, cell)) return true;
    }
    return false;
}
const Entity* Simulation::entity(EntityId id) const { if (const auto* u = unit(id)) return u; return building(id); }
Entity* Simulation::mutableEntity(EntityId id) { if (auto* u = mutableUnit(id)) return u; return mutableBuilding(id); }
Entity Simulation::makeEntity(const EntityDefinition& type, Cell cell, PlayerId owner) {
    Entity e;
    e.id = nextId_++; e.owner = owner; e.definitionId = type.id; e.definition = type;
    e.cell = e.origin = cell;
    e.position = e.tickPosition = type.collision == CollisionShape::Footprint ?
        Vec2{cell.x + type.width * .5f, cell.y + type.height * .5f} : center(cell);
    e.health = type.maximumHealth; e.mana = type.maximumMana;
    e.constructionProgress = type.constructionTicks;
    e.rally = defaultRally(type, cell);
    if (type.hero) e.hero = HeroProgression{1, 0, progression_};
    for (const auto& ability : type.abilities)
        if (ability.autocast && ability.autocast->enabledByDefault) e.autocastAbilities.push_back(ability.id);
    return e;
}
const Unit* Simulation::unit(EntityId id) const { for (const auto& u : units_) if (u.id == id) return &u; return nullptr; }
const Unit* Simulation::hero() const {
    for (const auto& u : units_) if (u.owner == player_.id && u.hero && u.health > 0) return &u;
    for (const auto& b : buildings_) if (b.owner == player_.id && b.hero && b.health > 0) return &b;
    return nullptr;
}
const Building* Simulation::building(EntityId id) const { for (const auto& b : buildings_) if (b.id == id) return &b; return nullptr; }
const Crystal* Simulation::crystal(EntityId id) const { for (const auto& node : crystals()) if (node.id == id) return &node; return nullptr; }
bool Simulation::crystalVisible(const Crystal& node) const {
    return crystalVisibleTo(player_.id, node);
}
bool Simulation::crystalVisibleTo(PlayerId id, const Crystal& node) const {
    for (int y = 0; y < node.height; ++y) for (int x = 0; x < node.width; ++x)
        if (visibleTo(id, node.cell + Cell{x, y})) return true;
    return false;
}
Unit* Simulation::mutableUnit(EntityId id) { for (auto& u : units_) if (u.id == id) return &u; return nullptr; }
Building* Simulation::mutableBuilding(EntityId id) { for (auto& b : buildings_) if (b.id == id) return &b; return nullptr; }
const Building* Simulation::buildingAt(Cell c) const { for (const auto& b : buildings_) if (b.contains(c)) return &b; return nullptr; }
const EntityDefinition& Simulation::entityType(const std::string& id) const {
    for (const auto& type : entityTypes_) if (type.id == id) return type;
    throw std::out_of_range("Unknown entity definition: " + id);
}
EntityId Simulation::spawn(const EntityDefinition& type, Cell cell, bool reserved, PlayerId owner) {
    if (type.collision != CollisionShape::Circle || type.width != 1 || type.height != 1) throw std::invalid_argument("Mobile runtime currently requires a one-cell footprint");
    if (!std::isfinite(type.collisionRadius) || type.collisionRadius < .05f || type.collisionRadius > .5f)
        throw std::invalid_argument("Collision radius must be in [0.05, 0.5] map units");
    if (!unitPositionFree(center(cell), type)) throw std::invalid_argument("Unit spawn is occupied");
    if (owner != neutralPlayer && !activePlayer(owner)) throw std::invalid_argument("Unit owner requires an active player slot");
    if (activePlayer(owner) && !reserved && !mutableEconomy(owner).supply.reserve(type.cost.supply)) throw std::invalid_argument("Initial army exceeds supply cap");
    auto u = makeEntity(type, cell, owner);
    units_.push_back(std::move(u));
    return units_.back().id;
}
std::vector<Simulation::WorldEvent> Simulation::takeEvents() { std::vector<WorldEvent> result; result.swap(events_); return result; }
bool Simulation::damageEnvironment(EntityId id, int damage) {
    if (damage <= 0) return false;
    for (const auto& object : environment())
        if (object.id == id && object.active() && object.interaction == Interaction::Destructible)
            return setEnvironmentHealth(id, std::max(0, object.hitPoints - damage));
    return false;
}
bool Simulation::setEnvironmentHealth(EntityId id, int hitPoints) {
    if (hitPoints < 0) return false;
    for (auto& object : scenario_.environment) {
        if (object.id != id) continue;
        const bool wasActive = object.active();
        object.hitPoints = std::min(hitPoints, object.maximumHitPoints);
        if (wasActive != object.active()) {
            for (int y = 0; y < object.height; ++y) for (int x = 0; x < object.width; ++x)
                if (object.blocks(x, y)) {
                    if (object.active()) scenario_.map.occupy(object.origin + Cell{x, y});
                    else scenario_.map.release(object.origin + Cell{x, y});
                }
            if (object.active()) events_.emplace_back(ObjectRestored{object.id, object.kind});
            else events_.emplace_back(ObjectDestroyed{object.id, object.kind});
            scenario_.map.rebuildVisionBlockers(environment());
            updateVision();
        }
        return true;
    }
    return false;
}
bool Simulation::environmentVisible(size_t index) const {
    const auto& object = environment().at(index);
    for (int y = 0; y < object.height; ++y) for (int x = 0; x < object.width; ++x)
        if (fog_.visible(object.origin + Cell{x, y})) return true;
    return false;
}
float Simulation::unitHeight(const Unit& u) const {
    return map().movementHeight(u.position, u.definition.movement);
}
void Simulation::revealMap() {
    fog_.revealAll();
    updateVision();
    setMessage(L"Вся карта открыта до конца матча.");
}
void Simulation::updateVision(bool force) {
    std::vector<VisionSource> sources;
    const bool day = clock_.phase() == DayPhase::Day;
    for (const auto& u : units_) if (u.owner == player_.id && u.health > 0) sources.push_back({u.cell, day ? u.definition.dayVision : u.definition.nightVision, airborne(u.definition.movement)});
    for (const auto& b : buildings_) if (b.owner == player_.id && b.health > 0) {
        const int radius = b.complete() ? (day ? b.definition.dayVision : b.definition.nightVision) : 2;
        sources.push_back({b.origin + Cell{b.definition.width / 2, b.definition.height / 2}, radius});
    }
    if (!force && sources == visionSources_) return;
    visionSources_ = std::move(sources);
    fog_.update(map(), visionSources_);
    for (size_t i = 0; i < crystals().size(); ++i) if (crystalVisible(crystals()[i])) knownCrystals_[i] = crystals()[i].remaining;
    for (size_t i = 0; i < environment().size(); ++i) if (environmentVisible(i)) knownEnvironment_[i] = environment()[i].active();
}
void Simulation::tick() {
    combatSearchBudget_ = 8;
    const auto phase = clock_.tick();
    if (phase) events_.emplace_back(*phase);
    // Run before workers: a newly initiated summon gets only its first tick from the builder.
    tickConstruction();
    // Advance the front of each moving group first, so IDs do not make a convoy wait backwards.
    std::vector<Unit*> updateOrder;
    for (auto& u : units_) {
        prepareAbilityTick(u);
        u.tickPosition = u.position; u.tickVelocity = u.velocity; u.velocity = {}; updateOrder.push_back(&u);
    }
    prepareMovement();
    arbitrateTraffic();
    replanMovement();
    std::stable_sort(updateOrder.begin(), updateOrder.end(), [](const Unit* a, const Unit* b) {
        if (a->moveGroup != b->moveGroup) return a->moveGroup < b->moveGroup;
        if (!a->moveGroup) return a->id < b->id;
        const auto f = a->formationForward;
        const float da = groundDot(a->position, f);
        const float db = groundDot(b->position, f);
        return da != db ? da > db : a->id < b->id;
    });
    for (auto* u : updateOrder) {
        tickUnit(*u);
        u->abilityAnimation.endTick();
        if (u->next >= u->route.size() || u->state == UnitState::Attacking ||
            u->attackPhase == AttackPhase::Windup || u->attackPhase == AttackPhase::Recovery) {
            u->motionFacing.reset(); // Preserve explicit arrival and combat orientations.
        } else {
            const float speed = u->moveGroup ? std::min(u->definition.movementPerSecond, u->groupSpeed) : u->definition.movementPerSecond;
            u->facing = u->motionFacing.update(u->facing, u->position - u->tickPosition, speed / ticksPerSecond);
        }
    }
    for (auto& b : buildings_) {
        prepareAbilityTick(b);
        if (b.health > 0 && b.complete()) tickUnit(b);
        b.abilityAnimation.endTick();
    }
    movementIndexActive_ = false;
    // Cell/radius changes must affect targeting and presentation in this tick.
    // An unchanged observer set does not need another full fog raster update.
    updateVision(false);
    tickCombat(); // Damage is simultaneous; removals happen after all movement pointers are no longer used.
    tickProduction();
    // Combat and production may remove or add observers. Periodic forced updates
    // also refresh remembered resources and externally changed terrain.
    updateVision(clock_.elapsedTicks() % 5 == 0);
}
}
