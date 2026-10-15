#include "rts/ecs/Entity.hpp"
#include <entt/entity/registry.hpp>

namespace rts {
components::Vitals& Entity::vitals() { return registry_->get<components::Vitals>(handle_); }
const components::Vitals& Entity::vitals() const { return registry_->get<components::Vitals>(handle_); }
components::Motion& Entity::motion() { return registry_->get<components::Motion>(handle_); }
const components::Motion& Entity::motion() const { return registry_->get<components::Motion>(handle_); }
components::Orders& Entity::orders() { return registry_->get<components::Orders>(handle_); }
const components::Orders& Entity::orders() const { return registry_->get<components::Orders>(handle_); }
components::Gathering& Entity::gathering() { return registry_->get<components::Gathering>(handle_); }
const components::Gathering& Entity::gathering() const { return registry_->get<components::Gathering>(handle_); }
components::Abilities& Entity::casting() { return registry_->get<components::Abilities>(handle_); }
const components::Abilities& Entity::casting() const { return registry_->get<components::Abilities>(handle_); }
components::Combat& Entity::combat() { return registry_->get<components::Combat>(handle_); }
const components::Combat& Entity::combat() const { return registry_->get<components::Combat>(handle_); }
components::Progression& Entity::progression() { return registry_->get<components::Progression>(handle_); }
const components::Progression& Entity::progression() const { return registry_->get<components::Progression>(handle_); }
components::Production& Entity::productionState() { return registry_->get<components::Production>(handle_); }
const components::Production& Entity::productionState() const { return registry_->get<components::Production>(handle_); }
}
