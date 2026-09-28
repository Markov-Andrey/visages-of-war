#include "rts/Environment.hpp"
#include <stdexcept>

namespace rts {
EnvironmentObject makeEnvironment(const std::string& type, EntityId id, Cell origin) {
    if (type == "TREE") return {id, EnvironmentKind::Tree, origin, 1, 1, {true}, Interaction::Destructible, 100, 100, true};
    if (type == "ROCK") return {id, EnvironmentKind::Rock, origin, 2, 2, {true, true, true, false}, Interaction::Destructible, 300, 300, true};
    if (type == "ARCH") return {id, EnvironmentKind::Arch, origin, 3, 1, {true, false, true}, Interaction::Destructible, 500, 500};
    throw std::runtime_error("Unknown environment object type");
}
}
