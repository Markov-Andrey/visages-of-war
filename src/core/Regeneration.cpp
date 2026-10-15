#include "rts/Simulation.hpp"

namespace rts {
void Simulation::tickRegeneration() {
    // Decay has no killer, but uses the same supply, collision and death events as combat.
    if (ecs::regenerateVitals(world_, clock_.elapsedTicks())) removeDeadEntities();
}
}
