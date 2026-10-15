#pragma once
#include "rts/ecs/Components.hpp"
#include <entt/entity/fwd.hpp>

namespace rts {
namespace ecs { class World; }
// Stable registry-owned identity and spatial anchor. Mechanic state lives in component pools.
struct Entity {
    static constexpr bool in_place_delete = true;
    Entity(entt::registry& registry, entt::entity handle) : registry_(&registry), handle_(handle) {}
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    EntityId id{};
    PlayerId owner{};
    std::string definitionId;
    EntityDefinition definition;
    Cell cell;
    Vec2 position;
    Cell facing{0, 1};
    static constexpr size_t productionQueueLimit = 10;
    Cell origin, rally;
    components::Vitals& vitals();
    const components::Vitals& vitals() const;
    components::Motion& motion();
    const components::Motion& motion() const;
    components::Orders& orders();
    const components::Orders& orders() const;
    components::Gathering& gathering();
    const components::Gathering& gathering() const;
    components::Abilities& casting();
    const components::Abilities& casting() const;
    components::Combat& combat();
    const components::Combat& combat() const;
    components::Progression& progression();
    const components::Progression& progression() const;
    components::Production& productionState();
    const components::Production& productionState() const;
    bool complete() const { return !definition.constructible || productionState().constructionProgress >= definition.constructionTicks; }
    bool training() const { return complete() && !productionState().production.empty() && productionState().production.front().remainingTicks > 0; }
    bool contains(Cell c) const {
        return c.x >= origin.x && c.y >= origin.y && c.x < origin.x + definition.width && c.y < origin.y + definition.height;
    }
    const AbilityDefinition* harvestAbility() const {
        const auto* selected = ability(orders().currentOrder.abilityId);
        return selected && selected->harvest() ? selected : nullptr;
    }
    int level() const { return progression().hero ? progression().hero->level : definition.level; }
    Vital maximumHealth() const { return definition.maximumHealth + (progression().hero ? (level() - 1) * definition.hero->healthPerLevel : 0); }
    Vital maximumMana() const { return definition.maximumMana; }
    const AbilityDefinition* ability(int abilityId) const {
        for (const auto& value : definition.abilities) if (value.id == abilityId) return &value;
        return nullptr;
    }
    int abilityCooldown(int abilityId) const {
        for (const auto& value : casting().abilityCooldowns) if (value.abilityId == abilityId) return value.remainingTicks;
        return 0;
    }
    bool autocastEnabled(int abilityId) const {
        return std::find(casting().autocastAbilities.begin(), casting().autocastAbilities.end(), abilityId) != casting().autocastAbilities.end();
    }
    int attackDamage() const { return definition.attackDamage + (progression().hero ? (level() - 1) * definition.hero->damagePerLevel : 0); }
    int armor() const { return definition.armor; }
    bool atMaxLevel() const { return progression().hero && level() == static_cast<int>(progression().hero->rules->thresholds.size()); }
    int experienceInLevel() const { return progression().hero ? progression().hero->experience - progression().hero->rules->thresholds[level() - 1] : 0; }
    int experienceToLevel() const { return progression().hero && !atMaxLevel() ? progression().hero->rules->thresholds[level()] - progression().hero->rules->thresholds[level() - 1] : 0; }
    float experienceFraction() const { return atMaxLevel() ? 1.0f : experienceToLevel() ? float(experienceInLevel()) / experienceToLevel() : 0.0f; }
private:
    entt::registry* registry_;
    entt::entity handle_;
};
using Unit = Entity;
using Building = Entity;
}
