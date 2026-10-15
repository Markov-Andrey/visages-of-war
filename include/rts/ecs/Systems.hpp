#pragma once
#include "rts/ecs/World.hpp"
#include "rts/Map.hpp"
#include <functional>
#include <span>

namespace rts::ecs {
enum class Phase { BeforeOrders, AfterOrders, AfterCombat, AfterRegeneration, AfterProduction };
struct SystemContext {
    World& entities;
    const Map& map;
    const PlayerSlots& players;
    std::span<PlayerEconomy> economies;
    std::uint64_t tick;
};
// Match-local extensions, ordered by phase, priority and stable module name.
// No globals, implicit observers or captured pointers to a movable Simulation.
class Systems {
public:
    using Function = std::function<void(SystemContext&)>;
    void add(std::string name, Phase phase, Function function, int priority = 0);
    bool run(Phase phase, SystemContext& context);
private:
    struct Entry { std::string name; Phase phase; Function function; int priority; };
    std::vector<Entry> entries_;
    bool running_{};
};
// Built-in component systems. Neither depends on the Simulation facade or UI.
void advanceAbilityTimers(World& world);
bool regenerateVitals(World& world, std::uint64_t tick);
}
