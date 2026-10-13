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
    u.automaticAbility = u.abilityWorking = false; u.repairProgress = 0;
    u.abilityAnimation.clear();
    if (u.replanQueued) std::erase(movementReplans_, u.id);
    u.progressPosition = u.position; u.progressTicks = u.stagnantTicks = 0;
    u.replanQueued = false; u.replanAfter = 0; u.relaxedAttempt = 0; u.yieldingTo = 0;
    if (order.kind == OrderKind::Patrol) u.patrolOrigin = u.position;
    u.moveGroup = order.moveGroup;
    if (!u.moveGroup) u.groupRadius = 0;
    u.velocity = {};
    u.motionFacing.reset();
    u.attackTarget = 0; cancelAttack(u); u.chaseTicks = 0;
    u.unreachableTarget = 0; u.targetRetryTicks = 0;
    u.route.clear(); u.next = 0; u.blockedTicks = 0;
    u.repeatGather = false; u.targetCrystal = -1; u.gatherOriginCrystal = -1; u.targetBuilding = 0; u.harvestTicks = 0;
    if (order.kind == OrderKind::Attack) {
        const auto target = combatTarget(order.target);
        if (canAttack(u, target, order.forceAttack) && targetVisible(u, target)) {
            u.attackTarget = target.id();
            chase(u, target);
        } else { u.route.clear(); u.next = 0; u.state = UnitState::Idle; }
        return;
    }
    if (order.kind == OrderKind::Stop || order.kind == OrderKind::Hold) { u.route.clear(); u.next = 0; u.state = UnitState::Idle; return; }
    if (order.kind == OrderKind::UseAbility) {
        u.targetBuilding = building(order.target) ? order.target : 0;
        if (path) followPath(u, std::move(*path), UnitState::ToAbility);
        else u.state = UnitState::Idle;
        return;
    }
    if (order.kind == OrderKind::AttackGround) { chaseGround(u); return; }
    if (order.kind == OrderKind::Build) {
        const auto* b = building(order.target);
        if (!b || b->owner != u.owner || b->complete() || !u.definition.canBuild) { u.route.clear(); u.next = 0; u.state = UnitState::Idle; return; }
        if (b->definition.constructionMode == ConstructionMode::Summoning && b->constructionProgress > 0) {
            applyOrder(u, {OrderKind::Stop}); return;
        }
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
    if (order.kind == OrderKind::Gather && u.definition.canGather()) {
        for (size_t i = 0; i < crystals().size(); ++i) {
            // Keep the clicked deposit as the origin throughout this gathering order.
            if (!crystals()[i].contains(order.cell)) continue;
            u.targetCrystal = u.gatherOriginCrystal = static_cast<int>(i); u.repeatGather = true;
            if (u.definition.harvesting().credit == HarvestCredit::Cargo && u.cargo >= u.definition.carryCapacity)
                returnCargo(u);
            else seekCrystal(u);
            commitAbility(u, *u.definition.gatherAbility);
            return;
        }
    }
    if (path) { u.blockedTicks = 0; followPath(u, std::move(*path), UnitState::Moving); }
    else if (!setRoute(u, std::span<const Cell>(&order.cell, 1), UnitState::Moving)) setMessage(L"Нет доступного пути.");
}
bool Simulation::command(Cell c) { if (units_.empty()) return false; const EntityId id = worker().id; return command(std::span<const EntityId>(&id, 1), c); }
bool Simulation::command(std::span<const EntityId> ids, Cell c, EntityId target) {
    if (!map().contains(c)) return false;
    if (!target) {
        for (const auto& candidate : units_) if (candidate.cell == c && candidate.health > 0 && fog_.visible(c)) {
            target = candidate.id;
            if (candidate.owner != player_.id) break;
        }
        if (!target) if (const auto* b = buildingAt(c)) target = b->id;
    }
    bool matched = false, accepted = false;
    std::vector<EntityId> visited;
    for (auto id : ids) {
        auto* u = mutableUnit(id);
        if (!u || u->owner != player_.id || std::find(visited.begin(), visited.end(), id) != visited.end()) continue;
        visited.push_back(id);
        if (const auto* ability = contextualAbility(*u, target)) {
            matched = true;
            accepted = activateAbility(id, ability->id, target) || accepted;
        }
    }
    // Like gathering and attacking, a contextual cast only orders eligible members.
    if (matched) return accepted;
    if (const auto* enemy = unit(target); enemy && enemy->owner != player_.id && fog_.visible(enemy->cell)) return attack(ids, target);
    auto kind = buildingAt(c) ? OrderKind::Interact : OrderKind::Move;
    for (const auto& node : crystals()) if (node.contains(c) && node.remaining > 0 && crystalVisible(node)) kind = OrderKind::Gather;
    return order(ids, kind, c);
}
bool Simulation::order(std::span<const EntityId> ids, OrderKind kind, Cell c) { return orderFor(player_.id, ids, kind, c); }
bool Simulation::orderFor(PlayerId actor, std::span<const EntityId> ids, OrderKind kind, Cell c) {
    if (!activePlayer(actor)) return false;
    if (!map().contains(c)) return false;
    if (kind == OrderKind::Attack || kind == OrderKind::Build || kind == OrderKind::UseAbility) return false;
    if (kind == OrderKind::Gather && std::none_of(crystals().begin(), crystals().end(),
        [&](const Crystal& crystal) { return crystal.contains(c) && crystal.remaining > 0 && crystalVisibleTo(actor, crystal); })) return false;
    std::vector<FormationMember> members;
    for (EntityId id : ids) {
        auto* u = mutableUnit(id);
        if (!u || u->owner != actor || std::any_of(members.begin(), members.end(), [&](const auto& m) { return m.id == id; })) continue;
        if (kind == OrderKind::Gather && !u->definition.canGather()) continue;
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
        float speed = 15, groupRadius = 0;
        std::vector<Unit*> participants;
        participants.reserve(destinations.size());
        for (const auto& destination : destinations) {
            auto* u = mutableUnit(destination.id);
            participants.push_back(u);
            speed = std::min(speed, u->definition.movementPerSecond);
            if (destinations.size() > 1)
                groupRadius = std::max(groupRadius, 1.75f * std::sqrt(groundLengthSquared(destination.position - center(c))));
        }
        for (size_t i = 0; i < destinations.size(); ++i) {
            const auto& destination = destinations[i];
            auto* u = participants[i];
            u->moveGroup = group; u->formationForward = destination.forward; u->groupSpeed = speed;
            u->groupTarget = c;
            u->groupRadius = groupRadius;
        }
        std::array<std::optional<RouteField>, 4> fields;
        for (size_t i = 0; i < destinations.size(); ++i) {
            const auto& destination = destinations[i];
            auto* u = participants[i];
            if (kind == OrderKind::Patrol && groundLengthSquared(destination.position - u->position) < 1e-8f) continue;
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
                    for (size_t j = 0; j < destinations.size(); ++j)
                        if (participants[j]->definition.movement == u->definition.movement) goals.push_back(destinations[j].cell);
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
    case UnitState::ToCrystal: {
        u.state = UnitState::Harvesting; u.harvestTicks = 0;
        if (u.definition.gatherAbility) u.abilityAnimation.sustain(*u.definition.gatherAbility, u.definition);
        if (u.targetCrystal >= 0) {
            const auto& node = crystals()[static_cast<size_t>(u.targetCrystal)];
            const Vec2 delta = Vec2{node.cell.x + node.width * .5f, node.cell.y + node.height * .5f} - u.position;
            const Cell facing{delta.x > .2f ? 1 : delta.x < -.2f ? -1 : 0, delta.y > .2f ? 1 : delta.y < -.2f ? -1 : 0};
            if (facing != Cell{}) u.facing = facing;
        }
        break;
    }
    case UnitState::ToBuild: u.state = UnitState::Building; break;
    case UnitState::ToAbility: u.state = UnitState::UsingAbility; break;
    case UnitState::ToHall: {
        const auto* depot = building(u.targetBuilding);
        if (depot && depot->complete() && depot->definition.acceptsCargo && depot->owner == u.owner) { mutableEconomy(u.owner).crystals += u.cargo; u.cargo = 0; }
        u.state = UnitState::Idle;
        if (u.repeatGather) seekCrystal(u);
        break;
    }
    default:
        u.state = UnitState::Idle;
        if (u.currentOrder.kind == OrderKind::Patrol &&
            groundLengthSquared(u.currentOrder.position.value_or(center(u.currentOrder.cell)) - u.patrolOrigin) > 1e-8f) {
            const Vec2 destination = u.currentOrder.position.value_or(center(u.currentOrder.cell));
            u.currentOrder.position = u.patrolOrigin; u.currentOrder.cell = cellAt(u.patrolOrigin);
            u.patrolOrigin = destination;
            resumeOrder(u);
        } else if (u.currentOrder.kind == OrderKind::AttackMove) u.currentOrder = {OrderKind::Stop};
        break;
    }
}
void Simulation::tickUnit(Unit& u) {
    tickAutocast(u);
    if (u.currentOrder.kind == OrderKind::UseAbility) {
        const auto* ability = u.ability(u.currentOrder.abilityId);
        if (!ability || !canTargetAbility(u, *ability, u.currentOrder.target)) { issue(u, {OrderKind::Stop}); return; }
        if (unit(u.currentOrder.target)) {
            if (repairReach(u, u.currentOrder.target, u.position)) {
                u.route.clear(); u.next = 0; u.velocity = {}; u.state = UnitState::UsingAbility;
            } else if (--u.chaseTicks <= 0) approachRepair(u);
        }
    }
    if (u.state == UnitState::ToBuild || u.state == UnitState::Building) {
        const auto* b = building(u.targetBuilding);
        if (!b || b->complete() || u.health <= 0 ||
            (b->definition.constructionMode == ConstructionMode::Summoning && b->constructionProgress > 0)) {
            issue(u, {OrderKind::Stop}); return;
        }
    }
    if (u.state == UnitState::Idle && u.moveGroup) yieldFormation(u);
    if (u.state == UnitState::Idle && !u.attackTarget &&
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
    if (u.state == UnitState::UsingAbility) { tickRepair(u); return; }
    if (u.state == UnitState::Building) {
        auto* b = mutableBuilding(u.targetBuilding);
        if (!b || b->complete()) { u.state = UnitState::Idle; return; }
        const auto positions = perimeter(map(), b->origin, b->definition.width, b->definition.height, u.definition.movement);
        if (std::find(positions.begin(), positions.end(), u.cell) == positions.end()) { u.state = UnitState::Idle; return; }
        advanceConstruction(*b);
        if (b->complete() || b->definition.constructionMode == ConstructionMode::Summoning)
            issue(u, {OrderKind::Stop});
        return;
    }
    harvest(u);
}
}
