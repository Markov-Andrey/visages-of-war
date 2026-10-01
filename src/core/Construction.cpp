#include "rts/Simulation.hpp"
#include <algorithm>
#include <array>

namespace rts {
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
bool Simulation::canPlace(const std::string& typeId, Cell origin) const {
    const auto& type = entityType(typeId);
    if (!type.constructible || type.factionId != workerType_.factionId) return false;
    if (!map().contains(origin)) return false;
    const int level = map().at(origin).height;
    for (int y = 0; y < type.height; ++y) for (int x = 0; x < type.width; ++x) {
        const Cell c = origin + Cell{x, y};
        if (!map().walkable(c) || map().at(c).surface != Surface::Land || !fog_.visible(c) || map().at(c).height != level || map().at(c).ramp != Cell{}) return false;
        for (const auto& u : units_) if (!airborne(u.definition.movement) &&
            (u.cell == c || (u.progress > 0 && u.next < u.route.size() && u.route[u.next] == c))) return false;
    }
    return true;
}
std::optional<EntityId> Simulation::construct(std::span<const EntityId> builders, const std::string& typeId, Cell origin) {
    const auto& type = entityType(typeId);
    if (stored_ < type.cost.crystals) { message_ = L"Недостаточно кристаллов."; return std::nullopt; }
    if (!canPlace(typeId, origin)) { message_ = L"Нужна видимая, свободная и ровная площадка."; return std::nullopt; }
    Map preview = map();
    for (int y = 0; y < type.height; ++y) for (int x = 0; x < type.width; ++x) preview.occupy(origin + Cell{x, y});
    Unit* builder = nullptr;
    for (EntityId id : builders) {
        auto* u = mutableUnit(id);
        if (!u || u->owner != player_.id || !u->definition.canBuild) continue;
        const Cell start = u->progress > 0 ? u->route[u->next] : u->cell;
        if (findPath(preview, start, perimeter(preview, origin, type.width, type.height, u->definition.movement), {}, u->definition.movement)) { builder = u; break; }
    }
    if (!builder) { message_ = L"Выберите рабочего, который может подойти к площадке."; return std::nullopt; }
    if (!supply_.reserve(type.cost.supply)) { message_ = L"Достигнут лимит армии: 100."; return std::nullopt; }
    scenario_.map = std::move(preview);
    const EntityId id = nextId_++;
    buildings_.push_back({id, player_.id, type, origin, 1, 0, defaultRally(type, origin), {}});
    stored_ -= type.cost.crystals;
    issue(*builder, {OrderKind::Build, {}, id});
    message_ = L"Стройплощадка размещена.";
    updateVision();
    return id;
}
bool Simulation::cancelConstruction(EntityId id) {
    auto it = std::find_if(buildings_.begin(), buildings_.end(), [&](const Building& b) { return b.id == id; });
    if (it == buildings_.end() || it->complete() || it->owner != player_.id) return false;
    for (int y = 0; y < it->definition.height; ++y) for (int x = 0; x < it->definition.width; ++x)
        scenario_.map.release(it->origin + Cell{x, y});
    stored_ += it->definition.cost.crystals; supply_.release(it->definition.cost.supply);
    for (auto& u : units_) if (u.targetBuilding == id || (u.pendingOrder && u.pendingOrder->target == id)) issue(u, {OrderKind::Stop});
    buildings_.erase(it); updateVision();
    message_ = L"Строительство отменено. Кристаллы возвращены.";
    return true;
}
bool Simulation::train(EntityId id, const std::string& definitionId) {
    auto* b = mutableBuilding(id);
    if (!b || b->owner != player_.id || !b->complete() || b->definition.trainableUnits.empty()) return false;
    if (b->production.size() >= 5) { message_ = L"Очередь производства заполнена."; return false; }
    const auto& choices = b->definition.trainableUnits;
    const auto& selected = definitionId.empty() ? choices.front() : definitionId;
    if (std::find(choices.begin(), choices.end(), selected) == choices.end()) return false;
    const auto& type = entityType(selected);
    if (!type.mobile || type.width != 1 || type.height != 1) { message_ = L"Этот тип пока нельзя выпустить как подвижную единицу."; return false; }
    if (stored_ < type.cost.crystals) { message_ = L"Недостаточно кристаллов."; return false; }
    if (!supply_.reserve(type.cost.supply)) { message_ = L"Достигнут лимит армии: 100."; return false; }
    stored_ -= type.cost.crystals;
    b->production.push_back({type.id, type.trainingTicks, type.trainingTicks, type.cost.crystals, type.cost.supply});
    message_ = L"Юнит добавлен в очередь.";
    return true;
}
bool Simulation::cancelTraining(EntityId id) {
    auto* b = mutableBuilding(id);
    if (!b || b->owner != player_.id || b->production.empty()) return false;
    const auto job = b->production.back();
    b->production.pop_back(); stored_ += job.paidCrystals; supply_.release(job.reservedSupply);
    message_ = L"Последний заказ отменён.";
    return true;
}
bool Simulation::setRally(EntityId id, Cell c) {
    auto* b = mutableBuilding(id);
    if (!b || b->owner != player_.id || b->definition.trainableUnits.empty()) return false;
    for (const auto& produced : b->definition.trainableUnits) {
        const auto movement = entityType(produced).movement;
        if (!map().walkable(c, movement)) { message_ = L"Точка сбора недоступна этому типу передвижения."; return false; }
        const auto exits = perimeter(map(), b->origin, b->definition.width, b->definition.height, movement);
        bool reachable = false;
        for (Cell exit : exits) if (findPath(map(), exit, c, movement)) { reachable = true; break; }
        if (!reachable) { message_ = L"Точка сбора недоступна."; return false; }
    }
    b->rally = c; message_ = L"Точка сбора установлена."; return true;
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
            bool held = false;
            for (const auto& u : units_) if (airborne(u.definition.movement) == airborne(type.movement) &&
                (u.cell == c || (u.progress > 0 && u.route[u.next] == c))) held = true;
            if (!held) { spawnCell = c; break; }
        }
        if (!spawnCell) continue; // Completed job waits for an exit; supply stays reserved.
        const EntityId id = spawn(type, *spawnCell, true);
        b.production.pop_front();
        events_.emplace_back(UnitProduced{b.id, id});
        const std::array<EntityId, 1> selected{id};
        command(selected, b.rally);
    }
}
}
