#pragma once
#include "rts/Simulation.hpp"
#include "rts/IconAsset.hpp"
#include <array>
#include <map>

namespace rts {
enum class UnitCommand { Move, Stop, Attack, Hold, Patrol, Gather, AttackGround, Build, Back };
enum class CommandScope { Selection, ActiveGroup };
inline constexpr size_t commandColumns = 4, commandRows = 4, commandSlots = commandColumns * commandRows;
inline constexpr size_t backCommandSlot = commandSlots - 1;
inline constexpr size_t firstAbilitySlot = 8;
inline constexpr std::array abilityKeys{L'Q', L'W', L'E'};
struct UnitCommandInfo {
    UnitCommand command;
    size_t slot;
    const char* icon;
    wchar_t key;
    const wchar_t* label;
    const wchar_t* description;
    OrderKind order;
    CommandScope scope = CommandScope::Selection;
};
inline constexpr std::array<UnitCommandInfo, 9> unitCommands{{
    {UnitCommand::Move, 0, "move", L'M', L"Идти", L"Идти к точке, не отвлекаясь на врагов.", OrderKind::Move},
    {UnitCommand::Stop, 1, "stop", L'S', L"Стоять", L"Отменить приказ. Юнит может сам вступить в бой с замеченным врагом.", OrderKind::Stop},
    {UnitCommand::Attack, 2, "attack", L'A', L"Атаковать", L"Выбрать врага или идти к точке, атакуя встреченных врагов.", OrderKind::AttackMove},
    {UnitCommand::Hold, 3, "hold", L'H', L"Позиция", L"Удерживать позицию: атаковать в пределах дальности без преследования.", OrderKind::Hold},
    {UnitCommand::Patrol, 4, "patrol", L'P', L"Патруль", L"Ходить между текущей позицией и точкой. После боя продолжать патруль.", OrderKind::Patrol},
    {UnitCommand::Gather, 5, "gather", L'G', L"Добывать", L"Выбрать видимую залежь кристаллов для добычи и доставки.", OrderKind::Gather, CommandScope::ActiveGroup},
    {UnitCommand::AttackGround, 6, "attack-ground", L'T', L"Обстрел", L"Обстреливать выбранную точку осадным оружием до нового приказа.", OrderKind::AttackGround, CommandScope::ActiveGroup},
    {UnitCommand::Build, 7, "build", L'B', L"Строить", L"Открыть список доступных построек.", OrderKind::Build, CommandScope::ActiveGroup},
    {UnitCommand::Back, backCommandSlot, "cancel", L'X', L"Назад", L"Отменить выбор команды или вернуться к приказам.", OrderKind::Stop}
}};
inline const UnitCommandInfo* unitCommandAt(size_t slot) {
    for (const auto& command : unitCommands) if (command.slot == slot) return &command;
    return nullptr;
}
inline OrderKind unitOrder(const Unit& unit) {
    const auto kind = unit.currentOrder.kind;
    if (kind == OrderKind::Hold || kind == OrderKind::Patrol || kind == OrderKind::AttackMove || kind == OrderKind::AttackGround) return kind;
    if (unit.targetUnit) return OrderKind::Attack;
    switch (unit.state) {
    case UnitState::Moving: return OrderKind::Move;
    case UnitState::ToBuild: case UnitState::Building: return OrderKind::Build;
    case UnitState::ToCrystal: case UnitState::Harvesting: case UnitState::ToHall: case UnitState::WaitingForCrystal: return OrderKind::Gather;
    default: return OrderKind::Stop;
    }
}
inline const wchar_t* orderName(OrderKind kind) {
    if (kind == OrderKind::Attack) return L"Атаковать";
    if (kind == OrderKind::AttackMove) return L"Идти и атаковать";
    if (kind == OrderKind::Hold) return L"Удерживать позицию";
    if (kind == OrderKind::AttackGround) return L"Удар по площади";
    if (kind == OrderKind::Interact) return L"Взаимодействовать";
    for (const auto& command : unitCommands) if (command.order == kind) return command.label;
    return L"Стоять";
}
inline bool commandCapable(const Unit& unit, UnitCommand command) {
    switch (command) {
    case UnitCommand::Attack: return unit.attackDamage() > 0;
    case UnitCommand::AttackGround: return unit.attackDamage() > 0 && unit.definition.projectile &&
        unit.definition.projectile->targeting == ProjectileTargeting::Point && unit.definition.projectile->splashRadius > 0;
    case UnitCommand::Gather: return unit.definition.carryCapacity > 0;
    case UnitCommand::Build: return unit.definition.canBuild;
    case UnitCommand::Back: return false;
    default: return true;
    }
}
inline bool idleWorker(const Simulation& game, const Unit& unit) {
    return unit.owner == game.player().id && unit.health > 0 && unit.definition.isWorker() &&
        unit.state == UnitState::Idle && !unit.targetUnit && unitOrder(unit) == OrderKind::Stop;
}
std::map<std::string, IconAsset> loadCommandIcons(const Paths& paths);
}
