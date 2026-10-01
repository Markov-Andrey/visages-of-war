#pragma once
#include "rts/Simulation.hpp"
#include "rts/Paths.hpp"
#include <array>
#include <map>

namespace rts {
enum class UnitCommand { Move, Stop, Attack, Hold, Patrol, Gather, AttackGround, Build, Back };
struct UnitCommandInfo {
    UnitCommand command;
    const char* icon;
    wchar_t key;
    const wchar_t* label;
    const wchar_t* description;
    OrderKind order;
};
inline constexpr std::array<UnitCommandInfo, 9> unitCommands{{
    {UnitCommand::Move, "move", L'M', L"Идти", L"Идти к точке, не отвлекаясь на врагов.", OrderKind::Move},
    {UnitCommand::Stop, "stop", L'S', L"Стоять", L"Отменить приказ. Юнит может сам вступить в бой с замеченным врагом.", OrderKind::Stop},
    {UnitCommand::Attack, "attack", L'A', L"Атаковать", L"Выбрать врага или идти к точке, атакуя встреченных врагов.", OrderKind::AttackMove},
    {UnitCommand::Hold, "hold", L'H', L"Позиция", L"Удерживать позицию: атаковать в пределах дальности без преследования.", OrderKind::Hold},
    {UnitCommand::Patrol, "patrol", L'P', L"Патруль", L"Ходить между текущей позицией и точкой. После боя продолжать патруль.", OrderKind::Patrol},
    {UnitCommand::Gather, "gather", L'G', L"Добывать", L"Выбрать видимую залежь кристаллов для добычи и доставки.", OrderKind::Gather},
    {UnitCommand::AttackGround, "attack-ground", L'T', L"Обстрел", L"Обстреливать выбранную точку осадным оружием до нового приказа.", OrderKind::AttackGround},
    {UnitCommand::Build, "build", L'B', L"Строить", L"Открыть список доступных построек.", OrderKind::Build},
    {UnitCommand::Back, "back", L'X', L"Назад", L"Отменить выбор команды или вернуться к приказам.", OrderKind::Stop}
}};
inline OrderKind unitOrder(const Unit& unit) {
    if (unit.pendingOrder) return unit.pendingOrder->kind;
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
        unit.state == UnitState::Idle && !unit.pendingOrder && !unit.targetUnit && unit.progress == 0 && unitOrder(unit) == OrderKind::Stop;
}
std::map<std::string, std::filesystem::path> loadCommandIcons(const Paths& paths);
}
