#include "rts/Simulation.hpp"
#include "rts/Formation.hpp"
#include <algorithm>
#include <array>

namespace rts {
std::vector<Cell> Simulation::occupied(const Unit& self, bool claimDestinations, bool ignoreGroup) const {
    std::vector<Cell> cells;
    for (const auto& u : units_) {
        if (u.id == self.id) continue;
        if (airborne(u.definition.movement) != airborne(self.definition.movement)) continue;
        if (ignoreGroup && self.moveGroup && self.moveGroup == u.moveGroup) continue;
        cells.push_back(u.cell);
        if (u.progress > 0 && u.next < u.route.size()) cells.push_back(u.route[u.next]);
        if (claimDestinations && !u.route.empty()) cells.push_back(u.route.back());
    }
    return cells;
}
bool Simulation::dynamicStep(const Unit& u, Cell to) const {
    if (!map().canStep(u.cell, to, u.definition.movement)) return false;
    const Cell d = to - u.cell;
    const float speed = u.moveGroup ? std::min(u.definition.movementPerSecond, u.groupSpeed) : u.definition.movementPerSecond;
    const float stepProgress = speed / ticksPerSecond / (d.x && d.y ? 1.41421356f : 1.0f);
    const auto onStep = [&](Cell c) {
        return c == to || (d.x && d.y && (c == Cell{u.cell.x + d.x, u.cell.y} || c == Cell{u.cell.x, u.cell.y + d.y}));
    };
    for (const auto& other : units_) {
        if (other.id == u.id || airborne(other.definition.movement) != airborne(u.definition.movement)) continue;
        const bool advancing = other.progress > 0 && other.next < other.route.size();
        if (!onStep(other.cell) && !(advancing && onStep(other.route[other.next]))) continue;
        // Following an already moving group neighbour is safe when both steps
        // are parallel and the leader will vacate its cell before we reach it.
        const float otherSpeed = other.moveGroup ? std::min(other.definition.movementPerSecond, other.groupSpeed) : other.definition.movementPerSecond;
        const bool parallel = u.moveGroup && u.moveGroup == other.moveGroup && advancing &&
            other.route[other.next] - other.cell == d && otherSpeed >= speed &&
            other.progress + .00001f >= stepProgress;
        if (!parallel || other.route[other.next] == to) return false;
    }
    return true;
}
bool Simulation::setRoute(Unit& u, std::span<const Cell> goals, UnitState state, bool waitForTraffic) {
    auto path = findPath(map(), u.cell, goals, occupied(u, true, true), u.definition.movement);
    if (!path && waitForTraffic) path = findPath(map(), u.cell, goals, {}, u.definition.movement);
    u.route.clear(); u.next = 0; u.progress = 0; u.blockedTicks = 0;
    if (!path) { u.state = UnitState::Idle; return false; }
    u.route = std::move(path->cells); u.next = 1; u.state = state;
    if (u.next == u.route.size()) arrived(u);
    return true;
}
void Simulation::issue(Unit& u, Order order) {
    if (u.progress > 0) u.pendingOrder = order;
    else applyOrder(u, order);
}
void Simulation::applyOrder(Unit& u, Order order) {
    u.moveGroup = order.moveGroup;
    u.targetUnit = 0; cancelAttack(u); u.chaseTicks = 0;
    u.repeatGather = false; u.targetCrystal = -1; u.gatherOriginCrystal = -1; u.targetBuilding = 0; u.harvestTicks = 0;
    if (order.kind == OrderKind::Attack) {
        const auto* target = unit(order.target);
        if (target && canAttack(u, *target) && targetVisible(u, *target)) {
            u.targetUnit = target->id;
            chase(u, *target);
        } else { u.route.clear(); u.next = 0; u.state = UnitState::Idle; }
        return;
    }
    if (order.kind == OrderKind::Stop) { u.route.clear(); u.next = 0; u.state = UnitState::Idle; return; }
    if (order.kind == OrderKind::Build) {
        const auto* b = building(order.target);
        if (!b || b->complete() || !u.definition.canBuild) { u.route.clear(); u.next = 0; u.state = UnitState::Idle; return; }
        u.targetBuilding = b->id;
        if (!setRoute(u, perimeter(map(), b->origin, b->definition.width, b->definition.height, u.definition.movement), UnitState::ToBuild))
            message_ = L"Рабочий не может добраться до стройки.";
        return;
    }
    if (order.kind == OrderKind::Interact) {
        if (const auto* b = buildingAt(order.cell)) {
            if (!b->complete() && u.definition.canBuild) { applyOrder(u, {OrderKind::Build, {}, b->id}); return; }
            if (b->complete() && b->definition.acceptsCargo && u.cargo > 0) { returnCargo(u); return; }
            setRoute(u, perimeter(map(), b->origin, b->definition.width, b->definition.height, u.definition.movement), UnitState::Moving);
            return;
        }
    }
    if (order.kind == OrderKind::Gather && u.definition.carryCapacity > 0) {
        for (size_t i = 0; i < crystals().size(); ++i) {
            // The command was validated when issued. It may be applied after a step,
            // when this deposit has already depleted or left current vision.
            if (crystals()[i].cell != order.cell) continue;
            u.targetCrystal = u.gatherOriginCrystal = static_cast<int>(i); u.repeatGather = true;
            if (u.cargo >= u.definition.carryCapacity) returnCargo(u); else seekCrystal(u);
            return;
        }
    }
    if (!setRoute(u, std::span<const Cell>(&order.cell, 1), UnitState::Moving)) message_ = L"Нет доступного пути.";
}
bool Simulation::command(Cell c) { if (units_.empty()) return false; const EntityId id = worker().id; return command(std::span<const EntityId>(&id, 1), c); }
bool Simulation::command(std::span<const EntityId> ids, Cell c) {
    if (!map().contains(c)) return false;
    for (const auto& target : units_) if (target.owner != player_.id && target.cell == c && fog_.visible(c)) return attack(ids, target.id);
    auto kind = buildingAt(c) ? OrderKind::Interact : OrderKind::Move;
    if (fog_.visible(c)) for (const auto& node : crystals()) if (node.cell == c && node.remaining > 0) kind = OrderKind::Gather;
    std::vector<FormationMember> members;
    for (EntityId id : ids) {
        auto* u = mutableUnit(id);
        if (!u || u->owner != player_.id || std::any_of(members.begin(), members.end(), [&](const auto& m) { return m.id == id; })) continue;
        members.push_back({id, u->progress > 0 ? u->route[u->next] : u->cell,
            u->definition.movement, u->definition.formationPriority, u->formationForward});
    }
    bool any = false;
    if (kind != OrderKind::Move) {
        for (const auto& member : members) { issue(*mutableUnit(member.id), {kind, c}); any = true; }
    } else {
        std::vector<FormationObstacle> held;
        for (const auto& u : units_) if (std::none_of(members.begin(), members.end(), [&](const auto& m) { return m.id == u.id; })) {
            held.push_back({u.cell, airborne(u.definition.movement)});
            if (u.progress > 0 && u.next < u.route.size()) held.push_back({u.route[u.next], airborne(u.definition.movement)});
        }
        const auto destinations = planFormation(map(), members, c, held);
        const auto group = nextMoveGroup_++;
        // Publish membership together before calculating any routes. Unit iteration order
        // must not turn the other selected members into stationary obstacles.
        float speed = 15;
        for (const auto& destination : destinations) speed = std::min(speed, unit(destination.id)->definition.movementPerSecond);
        for (const auto& destination : destinations) {
            auto* u = mutableUnit(destination.id);
            u->moveGroup = group; u->formationForward = destination.forward; u->groupSpeed = speed;
        }
        for (const auto& destination : destinations) {
            issue(*mutableUnit(destination.id), {OrderKind::Move, destination.cell, 0, group});
            any = true;
        }
    }
    message_ = any ? L"Приказ принят." : L"Нет доступного пути.";
    return any;
}
void Simulation::stop() { if (units_.empty()) return; const EntityId id = worker().id; stop(std::span<const EntityId>(&id, 1)); }
void Simulation::stop(std::span<const EntityId> ids) {
    for (EntityId id : ids) if (auto* u = mutableUnit(id); u && u->owner == player_.id) issue(*u, {OrderKind::Stop});
    message_ = L"Приказ остановки принят.";
}
void Simulation::arrived(Unit& u) {
    u.route.clear(); u.next = 0;
    if (u.moveGroup && u.state == UnitState::Moving) {
        constexpr std::array<Cell, 8> headings{{{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};
        const int heading = int(std::lround(std::atan2(u.formationForward.y, u.formationForward.x) / .7853981634f));
        u.facing = headings[(heading + 8) % 8];
    }
    switch (u.state) {
    case UnitState::ToAttack: u.state = UnitState::Attacking; break;
    case UnitState::ToCrystal: u.state = UnitState::Harvesting; u.harvestTicks = 0; break;
    case UnitState::ToBuild: u.state = UnitState::Building; break;
    case UnitState::ToHall: {
        const auto* depot = building(u.targetBuilding);
        if (depot && depot->complete() && depot->definition.acceptsCargo) { stored_ += u.cargo; u.cargo = 0; }
        u.state = UnitState::Idle;
        if (u.repeatGather) seekCrystal(u);
        break;
    }
    default: u.state = UnitState::Idle; break;
    }
}
void Simulation::tickUnit(Unit& u) {
    if (u.state == UnitState::WaitingForCrystal) {
        if (++u.blockedTicks >= ticksPerSecond) seekCrystal(u);
        return;
    }
    if (u.state == UnitState::ToCrystal && u.progress == 0 && u.targetCrystal >= 0 && crystals()[u.targetCrystal].remaining == 0) {
        seekCrystal(u);
        return;
    }
    if (u.next < u.route.size()) {
        const Cell destination = u.route[u.next];
        if (u.progress == 0 && !dynamicStep(u, destination)) {
            ++u.blockedTicks;
            const Cell step = destination - u.cell;
            const auto inWay = [&](Cell c) {
                return c == destination || (step.x && step.y && (c == Cell{u.cell.x + step.x, u.cell.y} || c == Cell{u.cell.x, u.cell.y + step.y}));
            };
            const bool waitingForGroup = u.moveGroup && std::any_of(units_.begin(), units_.end(), [&](const Unit& other) {
                return other.id != u.id && other.moveGroup == u.moveGroup && other.next < other.route.size() &&
                    (inWay(other.cell) || (other.progress > 0 && inWay(other.route[other.next])));
            });
            // Let a travelling neighbour clear its reserved step before taking a detour.
            if (u.blockedTicks >= (waitingForGroup ? 120 : 24) && u.blockedTicks % 15 == 9) {
                const Cell goal = u.route.back();
                if (auto path = findPath(map(), u.cell, std::span<const Cell>(&goal, 1), occupied(u), u.definition.movement)) {
                    u.route = std::move(path->cells); u.next = 1;
                    if (u.next == u.route.size()) { arrived(u); return; }
                }
            }
            if (u.blockedTicks >= (waitingForGroup ? 360 : 90)) {
                // A formation order keeps its slot while traffic clears. Losing the order
                // here would leave a stopped member blocking the rest of the column.
                if (u.moveGroup && u.state == UnitState::Moving && map().walkable(u.route.back(), u.definition.movement)) return;
                const auto state = u.state;
                u.route.clear(); u.next = 0; u.blockedTicks = 0;
                if (state == UnitState::ToHall) returnCargo(u);
                else if (state == UnitState::ToCrystal) seekCrystal(u);
                else u.state = UnitState::Idle;
            }
            return;
        }
        u.blockedTicks = 0;
        const Cell d = destination - u.cell;
        const float length = (d.x && d.y) ? 1.41421356f : 1.0f;
        const float oldProgress = u.progress;
        const float speed = u.moveGroup ? std::min(u.definition.movementPerSecond, u.groupSpeed) : u.definition.movementPerSecond;
        u.progress = std::min(1.0f, u.progress + speed / ticksPerSecond / length);
        u.facing = d;
        u.walkCycle = std::fmod(u.walkCycle + (u.progress - oldProgress) * length, 1.0f);
        u.position = center(u.cell) + (center(destination) - center(u.cell)) * u.progress;
        if (u.progress >= 1.0f) {
            u.cell = destination; u.progress = 0; ++u.next;
            if (u.pendingOrder) { const auto order = *u.pendingOrder; u.pendingOrder.reset(); applyOrder(u, order); }
            else if (u.next == u.route.size()) arrived(u);
        }
        return;
    }
    if (u.state == UnitState::Building) {
        auto* b = mutableBuilding(u.targetBuilding);
        if (!b || b->complete()) { u.state = UnitState::Idle; return; }
        const auto positions = perimeter(map(), b->origin, b->definition.width, b->definition.height, u.definition.movement);
        if (std::find(positions.begin(), positions.end(), u.cell) == positions.end()) { u.state = UnitState::Idle; return; }
        ++b->constructionProgress;
        b->health = std::max(1, b->definition.maximumHealth * b->constructionProgress / b->definition.constructionTicks);
        if (b->complete()) { events_.emplace_back(ConstructionFinished{b->id}); u.state = UnitState::Idle; message_ = L"Здание готово."; }
        return;
    }
    if (u.state != UnitState::Harvesting || u.targetCrystal < 0) return;
    auto& crystal = scenario_.crystals[static_cast<size_t>(u.targetCrystal)];
    if (crystal.remaining > 0 && ++u.harvestTicks >= ticksPerSecond / 3) {
        u.harvestTicks = 0; --crystal.remaining; ++u.cargo;
        if (crystal.remaining == 0) scenario_.map.release(crystal.cell);
    }
    if (u.cargo >= u.definition.carryCapacity || crystal.remaining == 0) {
        if (u.cargo > 0) returnCargo(u); else seekCrystal(u);
    }
}
}
