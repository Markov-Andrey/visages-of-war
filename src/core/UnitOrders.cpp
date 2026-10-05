#include "rts/Simulation.hpp"
#include "rts/Formation.hpp"
#include <algorithm>
#include <array>

namespace rts {
bool Simulation::setRoute(Unit& u, std::span<const Cell> goals, UnitState state, bool waitForTraffic) {
    const bool precise = goals.size() == 1 && u.currentOrder.position && goals.front() == u.currentOrder.cell && state == UnitState::Moving;
    const auto search = [&](std::span<const Circle> obstacles) {
        return precise ? findUnitPathTo(map(), u.position, *u.currentOrder.position, obstacles, u.definition.collisionRadius, u.definition.movement) :
            findUnitPath(map(), u.position, goals, obstacles, u.definition.collisionRadius, u.definition.movement);
    };
    auto path = search(unitObstacles(u, true, true));
    if (!path && waitForTraffic) path = search({});
    u.route.clear(); u.next = 0; u.blockedTicks = 0;
    if (!path) { u.state = UnitState::Idle; return false; }
    followPath(u, std::move(*path), state);
    return true;
}
void Simulation::issue(Unit& u, Order order) {
    applyOrder(u, order);
}
void Simulation::applyOrder(Unit& u, Order order, std::optional<Path> path) {
    u.currentOrder = order;
    if (order.kind == OrderKind::Patrol) u.patrolOrigin = u.position;
    u.moveGroup = order.moveGroup;
    if (!u.moveGroup) u.groupRadius = 0;
    u.velocity = {};
    u.motionFacing.reset();
    u.targetUnit = 0; cancelAttack(u); u.chaseTicks = 0;
    u.unreachableTarget = 0; u.targetRetryTicks = 0;
    u.route.clear(); u.next = 0; u.blockedTicks = 0;
    u.repeatGather = false; u.targetCrystal = -1; u.gatherOriginCrystal = -1; u.targetBuilding = 0; u.harvestTicks = 0;
    if (order.kind == OrderKind::Attack) {
        const auto* target = unit(order.target);
        if (target && canAttack(u, *target) && targetVisible(u, *target)) {
            u.targetUnit = target->id;
            chase(u, *target);
        } else { u.route.clear(); u.next = 0; u.state = UnitState::Idle; }
        return;
    }
    if (order.kind == OrderKind::Stop || order.kind == OrderKind::Hold) { u.route.clear(); u.next = 0; u.state = UnitState::Idle; return; }
    if (order.kind == OrderKind::AttackGround) { chaseGround(u); return; }
    if (order.kind == OrderKind::Build) {
        const auto* b = building(order.target);
        if (!b || b->owner != u.owner || b->complete() || !u.definition.canBuild) { u.route.clear(); u.next = 0; u.state = UnitState::Idle; return; }
        u.targetBuilding = b->id;
        if (!setRoute(u, perimeter(map(), b->origin, b->definition.width, b->definition.height, u.definition.movement), UnitState::ToBuild))
            setMessage(L"Рабочий не может добраться до стройки.");
        return;
    }
    if (order.kind == OrderKind::Interact) {
        if (const auto* b = buildingAt(order.cell)) {
            if (b->owner == u.owner && !b->complete() && u.definition.canBuild) { applyOrder(u, {OrderKind::Build, {}, b->id}); return; }
            if (b->owner == u.owner && b->complete() && b->definition.acceptsCargo && u.cargo > 0) { returnCargo(u); return; }
            setRoute(u, perimeter(map(), b->origin, b->definition.width, b->definition.height, u.definition.movement), UnitState::Moving);
            return;
        }
    }
    if (order.kind == OrderKind::Gather && u.definition.carryCapacity > 0) {
        for (size_t i = 0; i < crystals().size(); ++i) {
            // Keep the clicked deposit as the origin throughout this gathering order.
            if (!crystals()[i].contains(order.cell)) continue;
            u.targetCrystal = u.gatherOriginCrystal = static_cast<int>(i); u.repeatGather = true;
            if (u.cargo >= u.definition.carryCapacity) returnCargo(u); else seekCrystal(u);
            return;
        }
    }
    if (path) { u.blockedTicks = 0; followPath(u, std::move(*path), UnitState::Moving); }
    else if (!setRoute(u, std::span<const Cell>(&order.cell, 1), UnitState::Moving)) setMessage(L"Нет доступного пути.");
}
bool Simulation::command(Cell c) { if (units_.empty()) return false; const EntityId id = worker().id; return command(std::span<const EntityId>(&id, 1), c); }
bool Simulation::command(std::span<const EntityId> ids, Cell c) {
    if (!map().contains(c)) return false;
    for (const auto& target : units_) if (target.owner != player_.id && target.cell == c && fog_.visible(c)) return attack(ids, target.id);
    auto kind = buildingAt(c) ? OrderKind::Interact : OrderKind::Move;
    for (const auto& node : crystals()) if (node.contains(c) && node.remaining > 0 && crystalVisible(node)) kind = OrderKind::Gather;
    return order(ids, kind, c);
}
bool Simulation::order(std::span<const EntityId> ids, OrderKind kind, Cell c) {
    if (!map().contains(c)) return false;
    if (kind == OrderKind::Attack || kind == OrderKind::Build) return false;
    if (kind == OrderKind::Gather && std::none_of(crystals().begin(), crystals().end(),
        [&](const Crystal& crystal) { return crystal.contains(c) && crystal.remaining > 0 && crystalVisible(crystal); })) return false;
    std::vector<FormationMember> members;
    for (EntityId id : ids) {
        auto* u = mutableUnit(id);
        if (!u || u->owner != player_.id || std::any_of(members.begin(), members.end(), [&](const auto& m) { return m.id == id; })) continue;
        if (kind == OrderKind::Gather && u->definition.carryCapacity <= 0) continue;
        if (kind == OrderKind::AttackGround && (u->attackDamage() <= 0 || !u->definition.projectile ||
            u->definition.projectile->targeting != ProjectileTargeting::Point || u->definition.projectile->splashRadius <= 0)) continue;
        members.push_back({id, u->cell,
            u->definition.movement, u->definition.formationPriority, u->formationForward, u->definition.collisionRadius, u->position});
    }
    // The public click target is a tile. A solo patrol needs another tile;
    // grouped endpoints may share tiles but retain distinct continuous positions.
    if (kind == OrderKind::Patrol && members.size() == 1 && members.front().start == c) return false;
    bool any = false;
    if (kind != OrderKind::Move && kind != OrderKind::AttackMove && kind != OrderKind::Patrol) {
        for (const auto& member : members) { issue(*mutableUnit(member.id), {kind, c}); any = true; }
    } else {
        std::vector<FormationObstacle> held;
        for (const auto& u : units_) if (std::none_of(members.begin(), members.end(), [&](const auto& m) { return m.id == u.id; })) {
            held.push_back({u.cell, airborne(u.definition.movement), u.position, u.definition.collisionRadius});
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
            u->groupTarget = c;
            u->groupRadius = 0;
            if (destinations.size() > 1) for (const auto& d : destinations)
                u->groupRadius = std::max(u->groupRadius, 1.75f * std::sqrt(lengthSquared(d.position - center(c))));
        }
        std::array<std::optional<RouteField>, 4> fields;
        for (const auto& destination : destinations) {
            auto* u = mutableUnit(destination.id);
            if (kind == OrderKind::Patrol && lengthSquared(destination.position - u->position) < 1e-8f) continue;
            // Shared terrain guidance is enough for a moving crowd. Other units
            // are handled locally; their changing positions never invalidate this field.
            std::optional<Path> path;
            if (map().canTraverse(u->position, destination.position, u->definition.collisionRadius, u->definition.movement)) {
                // Preserve separate approach lines on open ground. Following a
                // multi-source field here would funnel everyone into its first slot.
                path = Path{{u->cell, destination.cell}, 0, destination.position};
            } else if (destinations.size() > 1) {
                auto& field = fields[static_cast<size_t>(u->definition.movement)];
                if (!field) {
                    std::vector<Cell> goals;
                    for (const auto& d : destinations) if (unit(d.id)->definition.movement == u->definition.movement) goals.push_back(d.cell);
                    field = makeRouteField(map(), goals, u->definition.movement);
                }
                path = fieldPath(map(), *field, u->position, destination.cell, u->definition.collisionRadius);
                if (path) { path->cells.push_back(destination.cell); path->destination = destination.position; }
            }
            applyOrder(*u, {kind, destination.cell, 0, group, destination.position}, std::move(path));
            any = true;
        }
    }
    setMessage(any ? L"" : L"Нет доступного пути.");
    return any;
}
void Simulation::stop() { if (units_.empty()) return; const EntityId id = worker().id; stop(std::span<const EntityId>(&id, 1)); }
void Simulation::stop(std::span<const EntityId> ids) {
    for (EntityId id : ids) if (auto* u = mutableUnit(id); u && u->owner == player_.id) issue(*u, {OrderKind::Stop});
    setMessage({});
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
    default:
        u.state = UnitState::Idle;
        if (u.currentOrder.kind == OrderKind::Patrol &&
            lengthSquared(u.currentOrder.position.value_or(center(u.currentOrder.cell)) - u.patrolOrigin) > 1e-8f) {
            const Vec2 destination = u.currentOrder.position.value_or(center(u.currentOrder.cell));
            u.currentOrder.position = u.patrolOrigin; u.currentOrder.cell = cellAt(u.patrolOrigin);
            u.patrolOrigin = destination;
            resumeOrder(u);
        } else if (u.currentOrder.kind == OrderKind::AttackMove) u.currentOrder = {OrderKind::Stop};
        break;
    }
}
void Simulation::tickUnit(Unit& u) {
    if (u.state == UnitState::Idle && u.moveGroup) yieldFormation(u);
    if (u.state == UnitState::Idle && !u.targetUnit &&
        (u.currentOrder.kind == OrderKind::Patrol || u.currentOrder.kind == OrderKind::AttackMove) && ++u.blockedTicks >= 15)
        resumeOrder(u);
    if (u.state == UnitState::WaitingForCrystal) {
        if (++u.blockedTicks >= ticksPerSecond) seekCrystal(u);
        return;
    }
    if (u.state == UnitState::ToCrystal && u.targetCrystal >= 0 && crystals()[u.targetCrystal].remaining == 0) {
        seekCrystal(u);
        return;
    }
    if (u.next < u.route.size()) {
        moveUnit(u);
        return;
    }
    if (u.state == UnitState::Building) {
        auto* b = mutableBuilding(u.targetBuilding);
        if (!b || b->complete()) { u.state = UnitState::Idle; return; }
        const auto positions = perimeter(map(), b->origin, b->definition.width, b->definition.height, u.definition.movement);
        if (std::find(positions.begin(), positions.end(), u.cell) == positions.end()) { u.state = UnitState::Idle; return; }
        ++b->constructionProgress;
        b->health = std::max(1, b->definition.maximumHealth * b->constructionProgress / b->definition.constructionTicks);
        if (b->complete()) { events_.emplace_back(ConstructionFinished{b->id}); u.state = UnitState::Idle; setMessage(L"Здание готово."); }
        return;
    }
    if (u.state != UnitState::Harvesting || u.targetCrystal < 0) return;
    auto& crystal = scenario_.crystals[static_cast<size_t>(u.targetCrystal)];
    if (crystal.remaining > 0 && ++u.harvestTicks >= ticksPerSecond / 3) {
        u.harvestTicks = 0; --crystal.remaining; ++u.cargo;
        if (crystal.remaining == 0)
            for (int y = 0; y < crystal.height; ++y) for (int x = 0; x < crystal.width; ++x)
                scenario_.map.release(crystal.cell + Cell{x, y});
    }
    if (u.cargo >= u.definition.carryCapacity || crystal.remaining == 0) {
        if (u.cargo > 0) returnCargo(u); else seekCrystal(u);
    }
}
}
