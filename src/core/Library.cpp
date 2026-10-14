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
std::string armorDescription(int armor, const ArmorRules& rules) {
    const double change = (rules.damageMultiplier(armor) - 1.0) * 100.0;
    // Do not display a rounded 100% immunity for finite armor.
    const double percent = armor >= 0 ? std::floor(-change * 100) / 100 : std::round(change * 100) / 100;
    return std::string(armor >= 0 ? "Снижение получаемого урона: " : "Дополнительный получаемый урон: ") +
        amount(std::abs(percent)) + "%";
}
std::string abilityTargetDetails(const AbilityDefinition& ability) {
    if (ability.targeting == AbilityTargeting::None) return "Без цели";
    std::string text;
    for (auto relation : ability.targets.relations) {
        if (!text.empty()) text += ", ";
        text += relation == TargetRelation::Own ? "Свои" : relation == TargetRelation::Enemy ? "Вражеские" : "Нейтральные";
    }
    if (ability.targeting != AbilityTargeting::Crystal) {
        if (ability.targets.damagedOnly) text += "; повреждённые";
        if (ability.targets.completedOnly) text += "; достроенные";
        if (!ability.targets.allowSelf) text += "; кроме себя";
    }
    return text;
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
    if (!entity.tags.ids().empty()) {
        std::string names;
        for (const auto& id : entity.tags.ids()) {
            if (!names.empty()) names += ", ";
            names += definitions.tagCatalog().tag(id).displayName;
        }
        stat("Теги", names);
    }
    stat("Здоровье", std::to_string(entity.maximumHealth));
    stat("Броня", std::to_string(entity.armor));
    result.back().tooltip = armorDescription(entity.armor, definitions.armorRules());
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
        for (const auto& id : entity.buildableTypes) paragraph(definitions.entity(id).displayName);
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
            stat("Цели", abilityTargetDescription(ability.targeting));
            if (ability.targeting != AbilityTargeting::None) stat("Условия выбора цели", abilityTargetDetails(ability));
            stat("Анимация способности", abilityAnimationName(ability.animation));
            if (ability.rightClick) stat("Применение по ПКМ", "По подходящей цели");
            stat("Дальность применения", ability.range > 0 ? amount(ability.range) : "Контакт");
            for (size_t i = 0; i < ability.effects.size(); ++i) {
                const auto& effect = ability.effects[i];
                stat("Эффект " + std::to_string(i + 1), effect.recipient == EffectRecipient::Caster ? "На себя" : "На выбранную цель");
                if (const auto* h = std::get_if<HarvestDefinition>(&effect.value)) {
                    stat("Анимация добычи", abilityAnimationName(ability.animation));
                    stat("За цикл добычи", std::to_string(h->amount), "crystal");
                    stat("Интервал добычи", seconds(h->intervalTicks));
                    stat("Зачисление", h->credit == HarvestCredit::Direct ? "Сразу в запас игрока" : "После доставки на склад");
                    if (h->credit == HarvestCredit::Cargo) stat("Вместимость груза", std::to_string(entity.carryCapacity), "crystal");
                } else if (const auto* repair = std::get_if<RepairDefinition>(&effect.value)) {
                    const bool buildingsOnly = effect.recipient == EffectRecipient::Target && ability.targeting == AbilityTargeting::Building;
                    stat("Время полного ремонта", std::to_string(repair->constructionTimePercent) + (buildingsOnly ? "% времени строительства" : "% времени строительства / обучения цели"));
                    stat("Цена полного ремонта", std::to_string(repair->constructionCostPercent) + (buildingsOnly ? "% цены здания" : "% цены цели"));
                } else if (const auto* restore = std::get_if<RestoreDefinition>(&effect.value)) {
                    stat(restore->stat == RestoredStat::Health ? "Восстановление здоровья" : "Восстановление маны", std::to_string(restore->amount));
                }
            }
            if (ability.autocast) {
                stat("Радиус автокаста", amount(ability.autocast->radius));
                stat("Проверка целей", seconds(ability.autocast->scanIntervalTicks));
                stat("Автокаст по умолчанию", ability.autocast->enabledByDefault ? "Включён" : "Выключен");
                stat("Условие автокаста", "Бездействие с приказом «Стоп»");
            }
        }
    }
    return result;
}
}
