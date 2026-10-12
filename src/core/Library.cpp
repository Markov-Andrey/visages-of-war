#include "rts/Library.hpp"
#include <iomanip>
#include <locale>
#include <sstream>

namespace rts {
namespace {
std::string amount(double value) {
    std::ostringstream result;
    result.imbue(std::locale::classic());
    result << std::fixed << std::setprecision(3) << value;
    auto text = result.str();
    while (text.back() == '0') text.pop_back();
    if (text.back() == '.') text.pop_back();
    return text;
}
std::string seconds(int ticks) { return amount(double(ticks) / simulationTicksPerSecond) + " с"; }
std::string movementName(MovementType type) {
    switch (type) {
    case MovementType::Walking: return "Наземное";
    case MovementType::Swimming: return "Плавание";
    case MovementType::Amphibious: return "Амфибия";
    case MovementType::Flying: return "Полёт";
    }
    return {};
}
std::string targetNames(const EntityDefinition& entity) {
    switch (entity.attackTargets) {
    case AttackTargets::All: return "Наземные и воздушные";
    case AttackTargets::Ground: return "Наземные";
    case AttackTargets::Air: return "Воздушные";
    case AttackTargets::SameLayer: return airborne(entity.movement) ? "Воздушные" : "Наземные";
    }
    return {};
}
}
std::vector<LibraryDetail> libraryDetails(const Definitions& definitions, const EntityDefinition& entity) {
    std::vector<LibraryDetail> result;
    const auto heading = [&](std::string text) { result.push_back({LibraryDetailKind::Heading, std::move(text), {}, {}}); };
    const auto paragraph = [&](std::string text) {
        if (!text.empty()) result.push_back({LibraryDetailKind::Paragraph, std::move(text), {}, {}});
    };
    const auto stat = [&](std::string label, std::string value, std::string icon = {}) {
        result.push_back({LibraryDetailKind::Stat, std::move(label), std::move(value), std::move(icon)});
    };
    result.push_back({LibraryDetailKind::Title, entity.displayName, {}, {}});
    paragraph(entity.description);
    stat("Здоровье", std::to_string(entity.maximumHealth));
    stat("Стоимость", std::to_string(entity.cost.crystals), "crystal");
    if (entity.cost.supply > 0) stat("Лимит армии", std::to_string(entity.cost.supply), "supply");
    stat("Обзор днём / ночью", std::to_string(entity.dayVision) + " / " + std::to_string(entity.nightVision) + " клеток");
    if (entity.maximumMana > 0) stat("Мана", std::to_string(entity.maximumMana));

    if (entity.attackDamage > 0) {
        heading("Бой");
        stat("Урон", std::to_string(entity.attackDamage));
        stat("Дальность атаки", amount(entity.attackRange) + " клеток");
        const int cycle = entity.attackWindupTicks + entity.attackRecoveryTicks + entity.attackCooldownTicks;
        stat("Темп атаки", amount(double(simulationTicksPerSecond) / std::max(1, cycle)) + " атак/с");
        stat("Полный цикл атаки", seconds(cycle));
        stat("Допустимые цели", targetNames(entity));
        if (entity.projectile) {
            const auto& projectile = *entity.projectile;
            stat("Наведение", projectile.targeting == ProjectileTargeting::Point ? "В точку выстрела" : "На цель");
            if (projectile.splashRadius > 0) {
                stat("Радиус поражения", amount(projectile.splashRadius) + " клеток");
                stat("Урон союзникам", projectile.friendlyFire ? "Да" : "Нет");
            }
        }
    }
    if (entity.mobile) {
        heading("Передвижение");
        stat("Тип движения", movementName(entity.movement));
        stat("Скорость", amount(entity.movementPerSecond) + " клеток/с");
    }
    if (entity.constructible) {
        heading("Строительство");
        stat("Размер", std::to_string(entity.width) + " × " + std::to_string(entity.height) + " клетки");
        stat("Время строительства", seconds(entity.constructionTicks));
        stat("Способ возведения", entity.constructionMode == ConstructionMode::Summoning ? "Призыв" : "Рабочим");
    }
    if (entity.canBuild) {
        heading("Рабочий");
        paragraph("Возводит здания.");
    }
    if (entity.canGather()) {
        const auto& ability = *entity.gatherAbility;
        const auto& harvest = entity.harvesting();
        heading(ability.displayName);
        paragraph(ability.description);
        stat("За цикл добычи", std::to_string(harvest.amount), "crystal");
        stat("Интервал добычи", seconds(harvest.intervalTicks));
        stat("Зачисление", harvest.credit == HarvestCredit::Direct ? "Сразу в запас игрока" : "После доставки на склад");
        if (harvest.credit == HarvestCredit::Cargo) stat("Вместимость груза", std::to_string(entity.carryCapacity), "crystal");
    }
    if (entity.acceptsCargo) paragraph("Принимает кристаллы, доставленные рабочими.");
    // Training is applicable only when a catalog producer actually offers this entity.
    const bool trainable = std::any_of(definitions.entities().begin(), definitions.entities().end(), [&](const auto& producer) {
        return std::find(producer.trainableUnits.begin(), producer.trainableUnits.end(), entity.id) != producer.trainableUnits.end();
    });
    if (trainable) stat("Время обучения", seconds(entity.trainingTicks));
    if (!entity.trainableUnits.empty()) {
        heading("Производство");
        for (const auto& id : entity.trainableUnits) {
            const auto& trainee = definitions.entity(id);
            stat(trainee.displayName, seconds(trainee.trainingTicks) + "; " + std::to_string(trainee.cost.crystals) +
                " крист.; лимит " + std::to_string(trainee.cost.supply));
        }
    }
    if (entity.hero) {
        heading("Развитие героя");
        paragraph("Здоровье и урон указаны для первого уровня.");
        stat("Максимальный уровень", std::to_string(definitions.progression().thresholds.size()));
        stat("Здоровье за уровень", "+" + std::to_string(entity.hero->healthPerLevel));
        stat("Урон за уровень", "+" + std::to_string(entity.hero->damagePerLevel));
    }
    if (!entity.abilities.empty()) {
        heading("Способности");
        for (const auto& ability : entity.abilities) {
            heading(ability.displayName);
            paragraph(ability.description);
            stat("Расход маны", std::to_string(ability.manaCost));
            stat("Перезарядка", seconds(ability.cooldownTicks));
        }
    }
    return result;
}
}
