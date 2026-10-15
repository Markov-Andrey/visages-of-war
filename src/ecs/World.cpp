#include "rts/ecs/World.hpp"
#include <entt/entity/registry.hpp>
#include <unordered_map>
#include <stdexcept>

namespace rts::ecs {
struct World::Storage {
    entt::registry registry;
    std::unordered_map<EntityId, entt::entity> ids;
    std::vector<Entity*> circles, footprints;
};
World::World() : storage_(std::make_unique<Storage>()) {}
World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;
entt::registry& World::registry() { return storage_->registry; }
const entt::registry& World::registry() const { return storage_->registry; }
Entity& World::create(EntityId id, const EntityDefinition& definition) {
    if (!id || storage_->ids.contains(id)) throw std::invalid_argument("Duplicate or zero ECS entity ID");
    auto& reg = registry();
    const auto handle = reg.create();
    try {
        reg.emplace<components::Vitals>(handle);
        reg.emplace<components::Motion>(handle);
        reg.emplace<components::Orders>(handle);
        reg.emplace<components::Gathering>(handle);
        reg.emplace<components::Abilities>(handle);
        reg.emplace<components::Combat>(handle);
        reg.emplace<components::Progression>(handle);
        reg.emplace<components::Production>(handle);
        auto& entity = reg.emplace<Entity>(handle, reg, handle);
        entity.id = id; entity.definitionId = definition.id; entity.definition = definition;
        auto& index = definition.collision == CollisionShape::Circle ? storage_->circles : storage_->footprints;
        storage_->ids.emplace(id, handle);
        try { index.push_back(&entity); }
        catch (...) { storage_->ids.erase(id); throw; }
        return entity;
    } catch (...) { reg.destroy(handle); throw; }
}
Entity* World::find(EntityId id) {
    const auto it = storage_->ids.find(id);
    return it == storage_->ids.end() ? nullptr : registry().try_get<Entity>(it->second);
}
const Entity* World::find(EntityId id) const {
    const auto it = storage_->ids.find(id);
    return it == storage_->ids.end() ? nullptr : registry().try_get<Entity>(it->second);
}
bool World::destroy(EntityId id) {
    const auto it = storage_->ids.find(id);
    if (it == storage_->ids.end()) return false;
    const auto handle = it->second;
    const auto* entity = registry().try_get<Entity>(handle);
    std::erase(storage_->circles, entity); std::erase(storage_->footprints, entity);
    storage_->ids.erase(it);
    registry().destroy(handle);
    return true;
}
EntityRange<Entity> World::circles() { return EntityRange<Entity>(storage_->circles); }
EntityRange<const Entity> World::circles() const { return EntityRange<const Entity>(storage_->circles); }
EntityRange<Entity> World::footprints() { return EntityRange<Entity>(storage_->footprints); }
EntityRange<const Entity> World::footprints() const { return EntityRange<const Entity>(storage_->footprints); }
}
