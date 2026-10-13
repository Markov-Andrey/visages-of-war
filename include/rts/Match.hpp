#pragma once
#include "rts/Types.hpp"
#include "rts/Weapons.hpp"
#include "rts/BuildingSprite.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace rts {
inline constexpr int simulationTicksPerSecond = 30;
using EntityId = std::uint32_t;
using PlayerId = std::uint8_t;
inline constexpr PlayerId neutralPlayer = 255;
inline constexpr size_t maximumPlayers = 10;
enum class PlayerSlotState { Empty, Closed, Player, AI };
struct PlayerSlot {
    PlayerSlotState state = PlayerSlotState::Empty;
    int startingCrystals{};
    bool active() const { return state == PlayerSlotState::Player || state == PlayerSlotState::AI; }
    bool operator==(const PlayerSlot&) const = default;
};
using PlayerSlots = std::array<PlayerSlot, maximumPlayers>;
// One local player and scripted opponents in the prototype's default map setup.
inline PlayerSlots defaultPlayerSlots() {
    PlayerSlots slots{};
    slots[0].state = PlayerSlotState::Player;
    slots[1].state = PlayerSlotState::AI;
    return slots;
}
inline void validatePlayerSlots(const PlayerSlots& slots) {
    for (const auto& slot : slots) {
        if (slot.state != PlayerSlotState::Empty && slot.state != PlayerSlotState::Closed &&
            slot.state != PlayerSlotState::Player && slot.state != PlayerSlotState::AI)
            throw std::invalid_argument("Invalid player slot state");
        if (slot.startingCrystals < 0 || slot.startingCrystals > 1000000 || (!slot.active() && slot.startingCrystals != 0))
            throw std::invalid_argument("Invalid player starting resources");
    }
}
enum class TeamColor {
    Blue, Red, Green, Purple, Orange, Cyan,
    Gold, Yellow, Lime, Emerald, Mint, Teal,
    Sky, Cobalt, Indigo, Violet, Magenta, Pink,
    Vermilion, Crimson, Fuchsia, Chartreuse, Spring, NeonGreen, White, Black, Burgundy, Count
};
struct TeamPalette { unsigned rgb; std::wstring_view name; };
inline constexpr std::array<TeamPalette, static_cast<size_t>(TeamColor::Count)> teamPalettes{{
    // 24 vivid hues, followed by white, black and wine red.
    {0x0000ff, L"Синий"}, {0xff0000, L"Красный"}, {0x00ff00, L"Зелёный"},
    {0x8000ff, L"Фиолетовый"}, {0xff8000, L"Оранжевый"}, {0x00ffff, L"Ледяной"},
    {0xffbf00, L"Золотой"}, {0xffff00, L"Жёлтый"}, {0x80ff00, L"Весенняя листва"},
    {0x00ff80, L"Изумрудный"}, {0x00ffbf, L"Мятный"}, {0x00bfff, L"Бирюзовый"},
    {0x0080ff, L"Лазурный"}, {0x0040ff, L"Королевский синий"}, {0x4000ff, L"Черничный"},
    {0xbf00ff, L"Пурпурный"}, {0xff00ff, L"Орхидейный"}, {0xff0080, L"Розовый"},
    {0xff4000, L"Киноварь"}, {0xff0040, L"Малиновый"}, {0xff00bf, L"Вересковый"},
    {0xbfff00, L"Лимонный"}, {0x40ff00, L"Молодая листва"}, {0x00ff40, L"Нефритовый"},
    {0xffffff, L"Белый"}, {0x000000, L"Чёрный"}, {0x800020, L"Винный"}
}};
struct PlayerSettings {
    PlayerId id = 0;
    std::string commanderId = "church_commander";
    TeamColor color = TeamColor::Blue;
};
inline unsigned teamRgb(TeamColor color) { return teamPalettes.at(static_cast<size_t>(color)).rgb; }
class ArmySupply {
public:
    static constexpr int maximum = 100;
    int used() const { return used_; }
    bool canReserve(int cost) const { return cost >= 0 && cost <= maximum - used_; }
    bool reserve(int cost) {
        if (!canReserve(cost)) return false;
        used_ += cost;
        return true;
    }
    void release(int cost) {
        if (cost < 0 || cost > used_) throw std::invalid_argument("Invalid army supply release");
        used_ -= cost;
    }
private:
    int used_{};
};
struct HeroDefinition { int healthPerLevel = 40; int damagePerLevel = 4; };
struct PlayerEconomy {
    std::int64_t crystals{};
    ArmySupply supply;
};
struct ProgressionRules {
    int experienceRadius = 8;
    int experiencePerVictimLevel = 50;
    std::vector<int> thresholds{0, 100, 250, 450, 700, 1000, 1400, 1900, 2500, 3200};
};
enum class HarvestCredit { Cargo, Direct };
struct HarvestEffectDefinition {
    unsigned color = 0xffc46a;
    float width = 1.2f, arcHeight = 12; // Screen pixels at zoom 1.
};
struct HarvestDefinition {
    HarvestCredit credit = HarvestCredit::Cargo;
    int intervalTicks = simulationTicksPerSecond / 3;
    int amount = 1;
    std::optional<HarvestEffectDefinition> effect;
};
enum class AbilityTargeting { None, Crystal, FriendlyBuilding, FriendlyUnit, FriendlyEntity };
inline std::string abilityTargetDescription(AbilityTargeting targeting) {
    switch (targeting) {
    case AbilityTargeting::None: return "Без цели";
    case AbilityTargeting::Crystal: return "Кристаллы";
    case AbilityTargeting::FriendlyBuilding: return "Свои здания";
    case AbilityTargeting::FriendlyUnit: return "Свои юниты и герои";
    case AbilityTargeting::FriendlyEntity: return "Свои здания, юниты и герои";
    }
    return {};
}
enum class AutocastCondition { Stopped };
enum class AbilityAnimation { Stand, Work, Attack };
inline std::string abilityAnimationName(AbilityAnimation animation) {
    switch (animation) {
    case AbilityAnimation::Stand: return "stand";
    case AbilityAnimation::Work: return "work";
    case AbilityAnimation::Attack: return "attack";
    }
    return {};
}
struct AutocastDefinition {
    float radius = 8;
    int scanIntervalTicks = 15;
    bool enabledByDefault{};
    AutocastCondition condition = AutocastCondition::Stopped;
};
struct RepairDefinition {
    int constructionTimePercent = 75, constructionCostPercent = 50;
};
struct AbilityDefinition {
    int id{};
    std::string displayName, description;
    int manaCost{}, cooldownTicks{};
    std::optional<HarvestDefinition> harvest;
    AbilityTargeting targeting = AbilityTargeting::None;
    std::optional<RepairDefinition> repair;
    std::optional<AutocastDefinition> autocast;
    bool rightClick{};
    AbilityAnimation animation = AbilityAnimation::Stand;
};
struct AbilityCooldown { int abilityId{}, remainingTicks{}; };
struct ResourceCost { int crystals{}, supply{}; };
// Cancellation returns half of the paid crystals, rounded down to whole crystals.
inline constexpr int cancellationRefund(int paidCrystals) { return paidCrystals / 2; }
enum class ConstructionMode { Worker, Summoning };
struct EntityDefinition {
    std::string id = "church.worker";
    std::string factionId = "church";
    std::string factionName = "Церковь Святого Пламени"; // Resolved from the faction catalog.
    std::string displayName = "Рабочий";
    std::string description;
    ResourceCost cost{40, 1};
    bool mobile = true;
    bool canDecompose = true;
    float movementPerSecond = 2.8f;
    float collisionRadius = .35f; // Logical map units, independent of sprite size.
    int carryCapacity = 10;
    std::optional<AbilityDefinition> gatherAbility; // Uses the Gather command slot, not Q/W/E.
    int trainingTicks = 90;
    int dayVision = 8, nightVision = 6;
    int maximumHealth = 60;
    int maximumMana{};
    std::vector<AbilityDefinition> abilities; // Resolved from the separate ability catalog.
    bool canBuild = true;
    MovementType movement = MovementType::Walking;
    int formationPriority = 1;
    int attackDamage = 0;
    float attackRange = 1.5f;
    int attackWindupTicks = 6, attackRecoveryTicks = 6, attackCooldownTicks = 18;
    AttackTargets attackTargets = AttackTargets::SameLayer;
    std::optional<ProjectileDefinition> projectile;
    UnitSpriteDefinition sprite;
    BuildingSpriteDefinition buildingSprite;
    int level = 1;
    std::optional<HeroDefinition> hero;
    bool canGather() const { return gatherAbility && gatherAbility->harvest; }
    const HarvestDefinition& harvesting() const {
        return gatherAbility.value().harvest.value();
    }
    bool isWorker() const { return canBuild || canGather(); }
    // Construction and mobility are independent capabilities, not exclusive kinds.
    bool constructible{};
    int width = 1, height = 1;
    int constructionTicks = 180;
    ConstructionMode constructionMode = ConstructionMode::Worker;
    bool acceptsCargo{};
    std::vector<std::string> trainableUnits;
    std::vector<std::string> alternateForms;
};
}
