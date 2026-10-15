#pragma once
#include "rts/ecs/Entity.hpp"
#include <iterator>
#include <memory>
#include <vector>

namespace rts::ecs {
// Ordered, non-owning index into ECS storage. Order is insertion order, never pool order.
template<class T> class EntityRange {
public:
    struct Iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::remove_const_t<T>;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;
        std::vector<Entity*>::const_iterator value;
        reference operator*() const { return **value; }
        pointer operator->() const { return *value; }
        Iterator& operator++() { ++value; return *this; }
        Iterator operator++(int) { auto old = *this; ++*this; return old; }
        bool operator==(const Iterator&) const = default;
    };
    explicit EntityRange(const std::vector<Entity*>& entries) : entries_(&entries) {}
    operator EntityRange<const Entity>() const requires (!std::is_const_v<T>) { return EntityRange<const Entity>(*entries_); }
    Iterator begin() const { return {entries_->begin()}; }
    Iterator end() const { return {entries_->end()}; }
    size_t size() const { return entries_->size(); }
    bool empty() const { return entries_->empty(); }
    T& operator[](size_t index) const { return *(*entries_)[index]; }
    T& at(size_t index) const { return *entries_->at(index); }
    T& front() const { return *entries_->front(); }
    T& back() const { return *entries_->back(); }
private:
    const std::vector<Entity*>* entries_;
};

// One owner of entity/component lifetime. Shape indexes contain no gameplay state.
class World {
public:
    World();
    ~World();
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    Entity& create(EntityId id, const EntityDefinition& definition);
    bool destroy(EntityId id);
    Entity* find(EntityId id);
    const Entity* find(EntityId id) const;
    EntityRange<Entity> circles();
    EntityRange<const Entity> circles() const;
    EntityRange<Entity> footprints();
    EntityRange<const Entity> footprints() const;
    // Systems include entt/entity/registry.hpp only in their implementation files.
    entt::registry& registry();
    const entt::registry& registry() const;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
