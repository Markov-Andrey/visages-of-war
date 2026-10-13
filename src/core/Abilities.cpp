#include "rts/Simulation.hpp"
#include <algorithm>
#include <limits>

namespace rts {
bool Simulation::activateAbility(EntityId id, int abilityId, EntityId target) {
    return activateAbilityFor(player_.id, id, abilityId, target);
}
bool Simulation::activateAbilityFor(PlayerId actor, EntityId id, int abilityId, EntityId target) {
    auto* unit = mutableEntity(id);
    if (!activePlayer(actor) || !unit || !unit->complete() || unit->health <= 0 || unit->owner != actor) return false;
    const auto* ability = unit->ability(abilityId);
    if (!ability) return false;
    return useAbility(*unit, *ability, target);
}
bool Simulation::useAbility(Unit& unit, const AbilityDefinition& ability, EntityId target, bool automatic) {
    if (unit.abilityCooldown(ability.id) > 0) { if (!automatic) setMessage(L"Способность ещё перезаряжается."); return false; }
    if (unit.mana < ability.manaCost) { if (!automatic) setMessage(L"Недостаточно маны."); return false; }
    if (!canTargetAbility(unit, ability, target)) { if (!automatic) setMessage(L"Способность нельзя применить к этой цели."); return false; }
    if (ability.harvest) {
        const auto* node = crystal(target);
        if (!node || (!unit.definition.mobile && !abilityReach(unit, ability, target, unit.position))) return false;
        Order order{OrderKind::Gather, node->cell, target}; order.abilityId = ability.id;
        applyOrder(unit, order);
        unit.automaticAbility = automatic;
    } else if (ability.targeting != AbilityTargeting::None && !startTargetedAbility(unit, ability, target, automatic)) {
        if (!automatic) setMessage(L"Цель вне досягаемости способности.");
        return false;
    }
    // Every successful effect, including an untargeted placeholder, commits here.
    commitAbility(unit, ability);
    if (!automatic) setMessage({});
    return true;
}
void Simulation::commitAbility(Unit& unit, const AbilityDefinition& ability) {
    unit.mana -= ability.manaCost;
    if (ability.cooldownTicks) unit.abilityCooldowns.push_back({ability.id, ability.cooldownTicks});
    unit.abilityAnimation.start(ability, unit.definition);
}
bool Simulation::setAutocast(EntityId id, int abilityId, bool enabled) {
    return setAutocastFor(player_.id, id, abilityId, enabled);
}
bool Simulation::setAutocastFor(PlayerId actor, EntityId id, int abilityId, bool enabled) {
    auto* u = mutableEntity(id);
    if (!activePlayer(actor) || !u || u->health <= 0 || u->owner != actor) return false;
    const auto* ability = u->ability(abilityId);
    if (!ability || !ability->autocast) return false;
    if (enabled && !u->autocastEnabled(abilityId)) u->autocastAbilities.push_back(abilityId);
    if (!enabled) {
        std::erase(u->autocastAbilities, abilityId);
        if (u->automaticAbility && u->currentOrder.abilityId == abilityId) issue(*u, {OrderKind::Stop});
    }
    return true;
}
bool Simulation::abilityEffectEligible(const Entity& caster, const AbilityDefinition& ability, EntityId target, bool automatic) const {
    if (ability.harvest) {
        const auto* node = crystal(target);
        return node && node->remaining > 0 && (ability.harvest->credit != HarvestCredit::Cargo || caster.definition.carryCapacity > 0);
    }
    if (ability.repair) {
        const auto* recipient = entity(target);
        if (!recipient || !recipient->complete() || recipient->health >= recipient->maximumHealth()) return false;
        // A manually ordered channel may wait for funds; automatic selection skips unaffordable work.
        if (automatic && economy(caster.owner).crystals == 0 && recipient->repairCredit <
            std::int64_t(recipient->definition.cost.crystals) * ability.repair->constructionCostPercent) return false;
    }
    return true;
}
bool Simulation::canTargetAbility(const Entity& u, const AbilityDefinition& ability, EntityId target) const {
    if (u.health <= 0 || !u.complete() || !activePlayer(u.owner)) return false;
    if (!abilityEffectEligible(u, ability, target, false)) return false;
    if (ability.targeting == AbilityTargeting::None) return target == 0;
    if (const auto* node = crystal(target)) {
        if (ability.targeting != AbilityTargeting::Crystal || !crystalVisibleTo(u.owner, *node)) return false;
        return std::find(ability.targets.relations.begin(), ability.targets.relations.end(), TargetRelation::Neutral) != ability.targets.relations.end();
    }
    const auto* recipient = entity(target);
    if (!recipient || recipient->health <= 0 || ability.targeting == AbilityTargeting::Crystal) return false;
    const bool footprint = recipient->definition.collision == CollisionShape::Footprint;
    if (ability.targeting != AbilityTargeting::Entity &&
        ability.targeting != (footprint ? AbilityTargeting::Building : AbilityTargeting::Unit)) return false;
    const auto relation = recipient->owner == u.owner ? TargetRelation::Own :
        recipient->owner == neutralPlayer ? TargetRelation::Neutral : TargetRelation::Enemy;
    const auto& filter = ability.targets;
    if (std::find(filter.relations.begin(), filter.relations.end(), relation) == filter.relations.end() ||
        (!filter.allowSelf && target == u.id) || (filter.completedOnly && !recipient->complete()) ||
        (filter.damagedOnly && recipient->health >= recipient->maximumHealth())) return false;
    if (!footprint) return visibleTo(u.owner, recipient->cell);
    for (int y = 0; y < recipient->definition.height; ++y) for (int x = 0; x < recipient->definition.width; ++x)
        if (visibleTo(u.owner, recipient->origin + Cell{x, y})) return true;
    return false;
}
const AbilityDefinition* Simulation::contextualAbility(const Unit& u, EntityId target) const {
    for (const auto& ability : u.definition.abilities)
        if (ability.rightClick && canTargetAbility(u, ability, target) && !u.abilityCooldown(ability.id) && u.mana >= ability.manaCost)
            return &ability;
    return nullptr;
}

namespace {
struct AbilityBounds { Cell origin; int width, height; Vec2 position; float radius; bool footprint; };
AbilityBounds bounds(const Simulation& game, EntityId id) {
    if (const auto* e = game.entity(id)) return {e->origin, e->definition.width, e->definition.height,
        e->position, e->definition.collisionRadius, e->definition.collision == CollisionShape::Footprint};
    const auto& c = *game.crystal(id);
    return {c.cell, c.width, c.height, {c.cell.x + c.width * .5f, c.cell.y + c.height * .5f}, 0, true};
}
Vec2 closestPoint(const AbilityBounds& b, Vec2 from) {
    if (!b.footprint) return b.position;
    const float x = float(b.origin.x), y = float(b.origin.y), right = x + b.width, bottom = y + b.height;
    if (from.x >= x && from.x <= right && from.y >= y && from.y <= bottom) return from;
    const std::array<Vec2, 4> corners{{{x,y}, {right,y}, {right,bottom}, {x,bottom}}};
    Vec2 best = corners.front();
    for (size_t i = 0; i < corners.size(); ++i) {
        const auto a = corners[i], edge = corners[(i + 1) % corners.size()] - a;
        const auto point = a + edge * std::clamp(groundDot(from - a, edge) / groundLengthSquared(edge), 0.f, 1.f);
        if (groundLengthSquared(point - from) < groundLengthSquared(best - from)) best = point;
    }
    return best;
}
}
std::vector<Cell> Simulation::abilityPositions(const Entity& u, const AbilityDefinition& ability, EntityId target) const {
    if (!entity(target) && !crystal(target)) return {};
    const auto b = bounds(*this, target);
    if (b.footprint && ability.range == 0) {
        auto positions = perimeter(map(), b.origin, b.width, b.height, u.definition.movement);
        if (!airborne(u.definition.movement)) std::erase_if(positions, [&](Cell c) { return map().at(c).height != map().at(b.origin).height; });
        return positions;
    }
    std::vector<Cell> positions;
    const float reach = ability.range > 0 ? ability.range : .75f;
    const int extent = static_cast<int>(std::ceil((reach + b.radius + u.definition.collisionRadius) * 2));
    const Cell origin = b.footprint ? b.origin : cellAt(b.position);
    for (int y = -extent; y <= (b.footprint ? b.height : 0) + extent; ++y)
        for (int x = -extent; x <= (b.footprint ? b.width : 0) + extent; ++x) {
            const Cell c = origin + Cell{x,y};
            if (map().walkable(c, u.definition.movement) && abilityReach(u, ability, target, center(c))) positions.push_back(c);
        }
    return positions;
}
bool Simulation::abilityReach(const Entity& u, const AbilityDefinition& ability, EntityId target, Vec2 position) const {
    if (target == u.id || ability.targeting == AbilityTargeting::None) return true;
    if (!entity(target) && !crystal(target)) return false;
    const auto b = bounds(*this, target);
    if (ability.range > 0 || u.definition.collision == CollisionShape::Footprint) {
        if (ability.range == 0 && !airborne(u.definition.movement) &&
            std::abs(map().at(u.origin).height - map().at(b.footprint ? b.origin : cellAt(b.position)).height) > .02f) return false;
        auto aim = closestPoint(b, position);
        auto start = position;
        if (u.definition.collision == CollisionShape::Footprint) {
            start = closestPoint(bounds(*this, u.id), aim);
            aim = closestPoint(b, start);
        }
        const float contact = (b.footprint ? 0.f : b.radius) +
            (u.definition.collision == CollisionShape::Circle ? u.definition.collisionRadius : 0.f);
        return groundLength(aim - start) <= contact + (ability.range > 0 ? ability.range : .75f);
    }
    if (!b.footprint) {
        const float contact = u.definition.collisionRadius + b.radius;
        const float distance = groundLength(position - b.position);
        return distance <= contact + .75f && distance >= contact &&
            map().canTraverse(position, b.position, .001f, u.definition.movement);
    }
    const auto positions = abilityPositions(u, ability, target);
    return std::any_of(positions.begin(), positions.end(), [&](Cell c) {
        return groundLengthSquared(position - center(c)) <= .0225f &&
            map().canTraverse(position, center(c), u.definition.collisionRadius, u.definition.movement);
    });
}
bool Simulation::startTargetedAbility(Entity& u, const AbilityDefinition& ability, EntityId target, bool automatic) {
    const bool reached = abilityReach(u, ability, target, u.position);
    if (!reached && !u.definition.mobile) return false;
    std::optional<Path> path;
    if (!reached) {
        const auto positions = abilityPositions(u, ability, target);
        path = findUnitPath(map(), u.position, positions, unitObstacles(u, true, true), u.definition.collisionRadius,
            u.definition.movement, automatic ? 2048 : 0);
        if (!path) path = findUnitPath(map(), u.position, positions, {}, u.definition.collisionRadius, u.definition.movement, automatic ? 2048 : 0);
        if (!path) return false;
    }
    Order order{OrderKind::UseAbility, cellAt(bounds(*this, target).position), target}; order.abilityId = ability.id;
    applyOrder(u, order, std::move(path));
    u.automaticAbility = automatic; u.chaseTicks = 15 + int(u.id % 7);
    if (reached) u.state = UnitState::UsingAbility;
    return true;
}
void Simulation::approachAbility(Entity& u) {
    const auto* ability = u.ability(u.currentOrder.abilityId);
    if (!ability || !u.definition.mobile) { issue(u, {OrderKind::Stop}); return; }
    u.chaseTicks = 15 + int(u.id % 7); u.abilityWorking = false;
    if (u.next < u.route.size() && abilityReach(u, *ability, u.currentOrder.target, u.routeDestination)) return;
    if (combatSearchBudget_ <= 0) { u.chaseTicks = 1; u.state = UnitState::ToAbility; return; }
    --combatSearchBudget_;
    const auto positions = abilityPositions(u, *ability, u.currentOrder.target);
    if (auto path = findUnitPath(map(), u.position, positions, unitObstacles(u, true, true), u.definition.collisionRadius, u.definition.movement, 2048))
        followPath(u, std::move(*path), UnitState::ToAbility);
    else { u.route.clear(); u.next = 0; u.state = UnitState::ToAbility; }
}
void Simulation::prepareAbilityTick(Entity& u) {
    u.abilityAnimation.beginTick();
    for (auto& cooldown : u.abilityCooldowns) --cooldown.remainingTicks;
    std::erase_if(u.abilityCooldowns, [](const auto& cooldown) { return cooldown.remainingTicks <= 0; });
}
void Simulation::tickAbility(Entity& u) {
    const auto* ability = u.ability(u.currentOrder.abilityId);
    if (!ability) return;
    if (ability->harvest) { harvest(u); return; }
    if (u.state != UnitState::UsingAbility) return;
    if (ability->repair) { tickRepair(u); return; }
    // An instantaneous targeted effect completes without cancelling its activation clip.
    u.state = UnitState::Idle; u.currentOrder = {OrderKind::Stop}; u.automaticAbility = false;
}
void Simulation::tickAutocast(Entity& u) {
    if (u.health <= 0 || !u.complete() || !activePlayer(u.owner) || u.state != UnitState::Idle ||
        u.currentOrder.kind != OrderKind::Stop || u.next < u.route.size() || u.attackTarget) return;
    for (const auto& ability : u.definition.abilities) {
        if (!ability.autocast || !u.autocastEnabled(ability.id) || u.abilityCooldown(ability.id) || u.mana < ability.manaCost) continue;
        const auto& automatic = *ability.autocast;
        if ((clock_.elapsedTicks() + u.id) % automatic.scanIntervalTicks) continue;
        if (ability.targeting == AbilityTargeting::None) { if (useAbility(u, ability, 0, true)) return; continue; }
        std::vector<std::pair<float, EntityId>> candidates;
        const auto consider = [&](EntityId id) {
            if (!canTargetAbility(u, ability, id)) return;
            if (!abilityEffectEligible(u, ability, id, true)) return;
            float distance = groundLengthSquared(closestPoint(bounds(*this, id), u.position) - u.position);
            if (abilityReach(u, ability, id, u.position)) distance = 0;
            else if (!u.definition.mobile) return;
            else if (ability.range == 0) {
                distance = std::numeric_limits<float>::max();
                for (Cell c : abilityPositions(u, ability, id)) distance = std::min(distance, groundLengthSquared(center(c) - u.position));
            }
            if (distance <= automatic.radius * automatic.radius) candidates.emplace_back(distance, id);
        };
        for (const auto& e : buildings_) consider(e.id);
        for (const auto& e : units_) consider(e.id);
        for (const auto& c : crystals()) consider(c.id);
        std::sort(candidates.begin(), candidates.end());
        for (const auto& candidate : candidates) if (useAbility(u, ability, candidate.second, true)) return;
    }
}
}
