#pragma once
#include "rts/Match.hpp"
#include "rts/HealthFeedback.hpp"
#include "rts/UnitFacing.hpp"
#include "rts/AbilityAnimation.hpp"
#include <deque>
#include <memory>

namespace rts {
enum class EntityKind { Unit, Building };
enum class UnitState { Idle, Moving, ToCrystal, Harvesting, ToHall, ToBuild, Building, WaitingForCrystal, ToAttack, Attacking, ToAbility, UsingAbility };
enum class OrderKind { Move, Interact, Stop, Build, Gather, Attack, AttackMove, Hold, Patrol, AttackGround, UseAbility };
struct Order { OrderKind kind; Cell cell{}; EntityId target{}; uint64_t moveGroup{}; std::optional<Vec2> position; int abilityId{}; bool forceAttack{}; };
struct HeroProgression {
    int level = 1; int experience{};
    std::shared_ptr<const ProgressionRules> rules;
};
struct HeroLevelChanged { EntityId id; int level; };
struct ProductionJob {
    std::string definitionId;
    int remainingTicks{}, totalTicks{}, paidCrystals{}, reservedSupply{};
};
struct AbilityEffectState { std::int64_t progress{}; int ticks{}; bool applied{}; };
namespace components {
struct Vitals {
    Vital health{}, mana{};
    int regenerationTicks{};
    std::int64_t repairCredit{}; // Prepaid restoration cost, retained across casters and orders.
    HealthFeedback healthFeedback;
};
struct Motion {
    Vec2 tickPosition; // Start of the current fixed tick, for relative collision sweeps.
    Vec2 velocity, tickVelocity; // Map units per second; prediction reads one common tick snapshot.
    MotionFacing motionFacing;
    float walkCycle{};
    uint64_t moveGroup{};
    Vec2 formationForward{0, 1};
    float groupSpeed{};
    Cell groupTarget{};
    float groupRadius{};
    std::vector<Cell> route;
    size_t next{};
    Vec2 routeDestination;
    int blockedTicks{};
    Vec2 progressPosition;
    int progressTicks{}, stagnantTicks{};
    bool replanQueued{};
    std::uint64_t replanAfter{};
    size_t relaxedAttempt{};
    uint64_t yieldingTo{};
    Vec2 yieldOrigin, yieldForward;
};
struct Orders {
    UnitState state = UnitState::Idle;
    EntityId targetBuilding{};
    Order currentOrder{OrderKind::Stop};
    Vec2 patrolOrigin;
};
struct Gathering {
    int cargo{};
    int harvestTicks{};
    int targetCrystal = -1;
    int gatherOriginCrystal = -1; // The clicked deposit remains the anchor across retries and deliveries.
    bool repeatGather{};
};
struct Abilities {
    std::vector<AbilityCooldown> abilityCooldowns;
    std::vector<int> autocastAbilities;
    bool automaticAbility{}, abilityWorking{}, abilityStarted{};
    AbilityAnimationPlayer abilityAnimation;
    std::vector<AbilityEffectState> abilityEffects;
};
struct Combat {
    EntityId attackTarget{};
    AttackPhase attackPhase = AttackPhase::Ready;
    int attackTicks{}, chaseTicks{};
    EntityId unreachableTarget{};
    int targetRetryTicks{};
};
struct Progression {
    std::optional<HeroProgression> hero;
};
struct Production {
    int constructionProgress{};
    std::deque<ProductionJob> production;
};
}
}
