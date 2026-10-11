#include "rts/Simulation.hpp"
#include <algorithm>
#include <array>

namespace rts {
bool Simulation::canPlace(const std::string& type, Cell origin) const { return canPlaceFor(player_.id, type, origin); }
std::optional<EntityId> Simulation::construct(std::span<const EntityId> builders, const std::string& type, Cell origin) {
    return constructFor(player_.id, builders, type, origin);
}
bool Simulation::cancelConstruction(EntityId id) { return cancelConstructionFor(player_.id, id); }
bool Simulation::train(EntityId id, const std::string& type) { return trainFor(player_.id, id, type); }
bool Simulation::cancelTraining(EntityId id, std::optional<size_t> index) { return cancelTrainingFor(player_.id, id, index); }

std::vector<Cell> Simulation::productionExits(const EntityDefinition& building, Cell origin, MovementType movement) const {
    auto exits = perimeter(map(), origin, building.width, building.height, movement);
    // Counterclockwise: bottom left to right, right bottom to top, top right to left, then left top to bottom.
    const auto index = [&](Cell cell) {
        const Cell local = cell - origin;
        if (local.y == building.height) return local.x;
        if (local.x == building.width) return building.width + building.height - 1 - local.y;
        if (local.y == -1) return 2 * building.width + building.height - 1 - local.x;
        return 2 * building.width + building.height + local.y;
    };
    std::sort(exits.begin(), exits.end(), [&](Cell a, Cell b) { return index(a) < index(b); });
    return exits;
}
Cell Simulation::defaultRally(const EntityDefinition& building, Cell origin) const {
    const auto movement = building.trainableUnits.empty() ? MovementType::Walking : entityType(building.trainableUnits.front()).movement;
    const auto exits = productionExits(building, origin, movement);
    return exits.empty() ? origin : exits.front();
}
bool Simulation::canPlaceFor(PlayerId actor, const std::string& typeId, Cell origin) const {
    if (!activePlayer(actor)) return false;
    const auto& type = entityType(typeId);
    if (!type.constructible || (actor == player_.id && type.factionId != workerType_.factionId)) return false;
    if (!map().contains(origin)) return false;
    const int level = map().at(origin).height;
    for (int y = 0; y < type.height; ++y) for (int x = 0; x < type.width; ++x) {
        const Cell c = origin + Cell{x, y};
        if (!map().walkable(c) || map().at(c).surface != Surface::Land || !visibleTo(actor, c) || map().at(c).height != level || map().at(c).ramp != Cell{}) return false;
        for (const auto& u : units_) if (!airborne(u.definition.movement) &&
            sweptCircleIntersectsCell(u.position, u.position, u.definition.collisionRadius, c)) return false;
    }
    return true;
}
std::optional<EntityId> Simulation::constructFor(PlayerId actor, std::span<const EntityId> builders, const std::string& typeId, Cell origin) {
    if (!activePlayer(actor)) return std::nullopt;
    auto& economy = mutableEconomy(actor);
    const auto& type = entityType(typeId);
    if (economy.crystals < type.cost.crystals) { setMessage(L"Недостаточно кристаллов."); return std::nullopt; }
    if (!canPlaceFor(actor, typeId, origin)) { setMessage(L"Нужна видимая, свободная и ровная площадка."); return std::nullopt; }
    Map preview = map();
    for (int y = 0; y < type.height; ++y) for (int x = 0; x < type.width; ++x) preview.occupy(origin + Cell{x, y});
    Unit* builder = nullptr;
    for (EntityId id : builders) {
        auto* u = mutableUnit(id);
        if (!u || u->owner != actor || !u->definition.canBuild || u->definition.factionId != type.factionId) continue;
        const Vec2 start = u->position;
        if (findUnitPath(preview, start, perimeter(preview, origin, type.width, type.height, u->definition.movement), {}, u->definition.collisionRadius, u->definition.movement)) { builder = u; break; }
    }
    if (!builder) { setMessage(L"Выберите рабочего, который может подойти к площадке."); return std::nullopt; }
    if (!economy.supply.reserve(type.cost.supply)) { setMessage(L"Достигнут лимит армии: 100."); return std::nullopt; }
    scenario_.map = std::move(preview);
    const EntityId id = nextId_++;
    buildings_.push_back({id, actor, type, origin, 1, 0, defaultRally(type, origin), {}});
    economy.crystals -= type.cost.crystals;
    issue(*builder, {OrderKind::Build, {}, id});
    setMessage(L"Стройплощадка размещена.");
    updateVision();
    return id;
}
bool Simulation::cancelConstructionFor(PlayerId actor, EntityId id) {
    if (!activePlayer(actor)) return false;
    auto& economy = mutableEconomy(actor);
    auto it = std::find_if(buildings_.begin(), buildings_.end(), [&](const Building& b) { return b.id == id; });
    if (it == buildings_.end() || it->complete() || it->owner != actor) return false;
    for (int y = 0; y < it->definition.height; ++y) for (int x = 0; x < it->definition.width; ++x)
        scenario_.map.release(it->origin + Cell{x, y});
    economy.crystals += cancellationRefund(it->definition.cost.crystals); economy.supply.release(it->definition.cost.supply);
    for (auto& u : units_) if (u.targetBuilding == id) issue(u, {OrderKind::Stop});
    buildings_.erase(it); updateVision();
    setMessage(L"Строительство отменено. Возвращено 50% стоимости.");
    return true;
}
bool Simulation::trainFor(PlayerId actor, EntityId id, const std::string& definitionId) {
    if (!activePlayer(actor)) return false;
    auto& economy = mutableEconomy(actor);
    auto* b = mutableBuilding(id);
    if (!b || b->owner != actor || !b->complete() || b->definition.trainableUnits.empty()) return false;
    if (b->production.size() >= Building::productionQueueLimit) { setMessage(L"Очередь производства заполнена."); return false; }
    const auto& choices = b->definition.trainableUnits;
    const auto& selected = definitionId.empty() ? choices.front() : definitionId;
    if (std::find(choices.begin(), choices.end(), selected) == choices.end()) return false;
    const auto& type = entityType(selected);
    if (!type.mobile || type.width != 1 || type.height != 1) { setMessage(L"Этот тип пока нельзя выпустить как подвижную единицу."); return false; }
    if (economy.crystals < type.cost.crystals) { setMessage(L"Недостаточно кристаллов."); return false; }
    if (!economy.supply.reserve(type.cost.supply)) { setMessage(L"Достигнут лимит армии: 100."); return false; }
    economy.crystals -= type.cost.crystals;
    b->production.push_back({type.id, type.trainingTicks, type.trainingTicks, type.cost.crystals, type.cost.supply});
    setMessage(L"Юнит добавлен в очередь.");
    return true;
}
bool Simulation::cancelTrainingFor(PlayerId actor, EntityId id, std::optional<size_t> queueIndex) {
    if (!activePlayer(actor)) return false;
    auto& economy = mutableEconomy(actor);
    auto* b = mutableBuilding(id);
    if (!b || b->owner != actor || b->production.empty()) return false;
    const size_t index = queueIndex.value_or(b->production.size() - 1);
    if (index >= b->production.size()) return false;
    const auto job = b->production[index];
    b->production.erase(b->production.begin() + index);
    economy.crystals += cancellationRefund(job.paidCrystals); economy.supply.release(job.reservedSupply);
    setMessage(L"Заказ отменён. Возвращено 50% стоимости.");
    return true;
}
bool Simulation::setRally(EntityId id, Cell c) {
    auto* b = mutableBuilding(id);
    if (!b || b->owner != player_.id || b->definition.trainableUnits.empty()) return false;
    for (const auto& produced : b->definition.trainableUnits) {
        const auto movement = entityType(produced).movement;
        if (!map().walkable(c, movement)) { setMessage(L"Точка сбора недоступна этому типу передвижения."); return false; }
        const auto exits = perimeter(map(), b->origin, b->definition.width, b->definition.height, movement);
        bool reachable = false;
        for (Cell exit : exits) if (findPath(map(), exit, c, movement)) { reachable = true; break; }
        if (!reachable) { setMessage(L"Точка сбора недоступна."); return false; }
    }
    b->rally = c; setMessage(L"Точка сбора установлена."); return true;
}
void Simulation::advanceConstruction(Building& b) {
    if (b.health <= 0 || b.complete()) return;
    const auto builtHealth = [&] {
        return std::max(1, int(std::int64_t(b.definition.maximumHealth) * b.constructionProgress / b.definition.constructionTicks));
    };
    const int previousHealth = builtHealth();
    ++b.constructionProgress;
    b.health = std::min(b.definition.maximumHealth, b.health + builtHealth() - previousHealth);
    if (b.complete()) {
        events_.emplace_back(ConstructionFinished{b.id});
        setMessage(L"Здание готово.");
    }
}
void Simulation::tickConstruction() {
    for (auto& b : buildings_)
        if (b.definition.constructionMode == ConstructionMode::Summoning && b.constructionProgress > 0)
            advanceConstruction(b);
}
void Simulation::tickProduction() {
    for (auto& b : buildings_) {
        if (!b.complete() || b.production.empty()) continue;
        auto& job = b.production.front();
        if (job.remainingTicks > 0) --job.remainingTicks;
        if (job.remainingTicks != 0) continue;
        const auto& type = entityType(job.definitionId);
        const auto exits = productionExits(b.definition, b.origin, type.movement);
        std::optional<Cell> spawnCell;
        for (Cell c : exits) {
            if (unitPositionFree(center(c), type)) { spawnCell = c; break; }
        }
        if (!spawnCell) continue; // Completed job waits for an exit; supply stays reserved.
        const EntityId id = spawn(type, *spawnCell, true, b.owner);
        b.production.pop_front();
        events_.emplace_back(UnitProduced{b.id, id});
        const std::array<EntityId, 1> selected{id};
        if (b.owner == player_.id) command(selected, b.rally);
        else {
            auto kind = buildingAt(b.rally) ? OrderKind::Interact : OrderKind::Move;
            for (const auto& node : crystals()) if (node.contains(b.rally) && node.remaining > 0 && crystalVisibleTo(b.owner, node)) kind = OrderKind::Gather;
            orderFor(b.owner, selected, kind, b.rally);
        }
    }
}
}
