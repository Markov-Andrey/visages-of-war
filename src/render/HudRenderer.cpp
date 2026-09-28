#include "RenderSupport.hpp"
#include <iomanip>
#include <sstream>

namespace rts {
using namespace render;
void Renderer::hud(const Simulation& game, const GameplayUi& ui, bool paused, const WorldView& view, bool grid) {
    const auto extent = size();
    const BattleLayout layout(extent);
    const auto panel = [&](UiRect area, unsigned color) {
        brush_->SetColor(D2D1::ColorF(color));
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect(area.x, area.y, area.width, area.height), 6, 6), brush_.Get());
    };
    panel({0, 0, extent.x, 58}, 0x101c25);
    panel({0, extent.y - 204, extent.x, 204}, 0x101c25);
    line({0, 58}, {extent.x, 58}, 0x31464c);
    line({0, extent.y - 204}, {extent.x, extent.y - 204}, 0x31464c);
    panel(layout.menu, layout.menu.contains(ui.mouse) ? 0x304851 : 0x1d303a);
    text(L"Меню / Esc", rect(29, 19, 94, 25), 0xd0ddd8);
    const int minutes = static_cast<int>(game.clock().minuteOfDay());
    std::wostringstream clockText;
    clockText << (game.clock().phase() == DayPhase::Day ? L"ДЕНЬ   " : L"НОЧЬ   ")
              << std::setfill(L'0') << std::setw(2) << minutes / 60 << L":" << std::setw(2) << minutes % 60;
    text(paused ? L"ПАУЗА  /  Пробел" : clockText.str(), rect(145, 20, 220, 25), 0xaabfbd);
    text(grid ? L"G — сетка: вкл." : L"G — сетка: выкл.", rect(360, 21, 160, 25), 0x7d9b99);
    text(L"КРИСТАЛЛЫ  " + std::to_wstring(game.storedCrystals()), rect(extent.x - 455, 16, 245, 35), 0xccb3f4, true);
    text(L"АРМИЯ  " + std::to_wstring(game.armySupply().used()) + L" / 100", rect(extent.x - 188, 21, 185, 27), 0xaadacb);
    const auto armyCount = std::count_if(game.units().begin(), game.units().end(), [&](const Unit& u) {
        return u.owner == game.player().id && !u.definition.isWorker();
    });
    panel(layout.army, layout.army.contains(ui.mouse) ? 0x36564f : 0x233d3d);
    text(L"F1  Армия  /  " + std::to_wstring(armyCount), rect(layout.army.x + 12, layout.army.y + 9, 144, 25), 0xdde8d9);
    const auto* hero = game.hero();
    const auto heroArea = layout.hero;
    panel(heroArea, !hero ? 0x303236 : heroArea.contains(ui.mouse) ? 0x535044 : 0x383d35);
    if (hero) {
        unitPortrait(hero->definition.sprite, {heroArea.x, heroArea.y + 18}, {58,58}, teamColor_);
        text(L"F2  " + wide(hero->definition.displayName), rect(heroArea.x + 9, heroArea.y + 4, 147, 24), 0xebd693);
        text(L"Уровень " + std::to_wstring(hero->level()), rect(heroArea.x + 58, heroArea.y + 28, 102, 23), 0xe2dfc7);
        text(std::to_wstring(hero->health) + L" / " + std::to_wstring(hero->maximumHealth()), rect(heroArea.x + 58, heroArea.y + 50, 102, 23), 0x99d8ad);
        panel({heroArea.x + 8, heroArea.y + 76, 148, 4}, 0x17222a);
        panel({heroArea.x + 8, heroArea.y + 76, 148 * hero->experienceFraction(), 4}, 0xaca0ec);
    } else {
        text(L"F2  Герой", rect(heroArea.x + 12, heroArea.y + 12, 140, 24), 0x88898c);
        text(game.heroFallen() ? L"Погиб" : L"Нет героя", rect(heroArea.x + 12, heroArea.y + 43, 140, 24), 0x717377);
    }
    panel({12, extent.y - 238, std::min(800.0f, extent.x - 24), 28}, 0x15262d);
    const std::wstring hint = !ui.placement.empty() ? L"Размещение: " + wide(game.entityType(ui.placement).displayName) + L"  •  ЛКМ — строить  •  ПКМ / Esc — отмена" :
        ui.rallyMode ? L"ЛКМ по карте — точка сбора. ПКМ / Esc — отмена." : game.message();
    text(hint, rect(24, extent.y - 233, extent.x - 48, 25), 0xb8cbc5);
    drawMinimap(game, layout, view);
    const auto info = layout.info;
    const Building* building = ui.selection.ids.size() == 1 ? game.building(ui.selection.ids.front()) : nullptr;
    const Unit* unit = ui.selection.ids.empty() ? nullptr : game.unit(ui.selection.ids.front());
    const Crystal* resource = ui.selection.ids.size() == 1 ? game.crystal(ui.selection.ids.front()) : nullptr;
    if (resource && (!game.fog().visible(resource->cell) || resource->remaining <= 0)) resource = nullptr;
    const EntityDefinition* tooltip = hero && heroArea.contains(ui.mouse) ? &hero->definition : nullptr;
    std::array<const EntityDefinition*, 6> commandTypes{};
    if (building && info.contains(ui.mouse)) tooltip = &building->definition;
    std::array<std::wstring, 6> labels{}, details{};
    if (resource) {
        const int remaining = resource->remaining;
        text(L"Кристаллы", rect(info.x, info.y, info.width, 34), 0xe0eade, true);
        text(L"Нейтральный объект", rect(info.x, info.y + 39, info.width, 24), 0x9dc1b6);
        sprite(crystal_.Get(), rect(0, 0, 128, 128), {info.x - 12, info.y + 48}, {96, 96});
        text(L"Остаток ресурса:  " + std::to_wstring(remaining) + L" / " + std::to_wstring(Crystal::maximum),
            rect(info.x + 88, info.y + 78, info.width - 88, 26), 0xcbb5ef);
        const float barWidth = std::max(1.0f, info.width - 100);
        panel({info.x + 88, info.y + 113, barWidth, 8}, 0x26313e);
        if (remaining > 0) panel({info.x + 88, info.y + 113, barWidth * remaining / Crystal::maximum, 8}, 0xb695db);
        text(L"Рабочие добывают ресурс и доставляют его в ратушу.",
            rect(info.x, info.y + 143, info.width, 23), 0x7e9eaa);
    } else if (building) {
        const auto& b = *building;
        text(wide(b.definition.displayName), rect(info.x, info.y, info.width, 34), 0xe0eade, true);
        text(L"Прочность  " + std::to_wstring(b.health) + L" / " + std::to_wstring(b.definition.maximumHealth), rect(info.x, info.y + 39, info.width, 25), 0x9dc1b6);
        if (!b.complete()) {
            text(L"Строительство  " + std::to_wstring(b.constructionProgress * 100 / b.definition.constructionTicks) + L"%", rect(info.x, info.y + 70, info.width, 28), 0xe4c388);
            labels[5] = L"X   Отменить"; details[5] = L"Вернуть ресурс";
        } else if (!b.definition.trainableUnits.empty()) {
            constexpr std::array keys{L"Q   ", L"E   ", L"T   "};
            for (size_t choice = 0; choice < b.definition.trainableUnits.size(); ++choice) {
                const auto slot = choice * 2;
                const auto& type = game.entityType(b.definition.trainableUnits[choice]);
                commandTypes[slot] = &type;
                labels[slot] = keys[choice] + wide(type.displayName);
                details[slot] = std::to_wstring(type.cost.crystals) + L" кр. / " + std::to_wstring(type.cost.supply) + L" лим.";
            }
            labels[1] = L"R   Сбор"; details[1] = L"ПКМ по карте";
            if (!b.production.empty()) { labels[5] = L"X   Отменить"; details[5] = L"Последний заказ"; }
            text(L"Очередь  " + std::to_wstring(b.production.size()) + L" / 5", rect(info.x, info.y + 72, info.width, 24), 0x9eb6b7);
            for (size_t i = 0; i < b.production.size(); ++i) {
                const auto& job = b.production[i];
                const float x = info.x + i * 55;
                panel({x, info.y + 102, 46, 42}, i == 0 ? 0x315b53 : 0x20363d);
                text(std::to_wstring(i + 1), rect(x + 17, info.y + 108, 25, 25), 0xd7e8df);
                brush_->SetColor(D2D1::ColorF(0x88d2b6));
                target_->FillRectangle(rect(x, info.y + 140, 46 * (1 - float(job.remainingTicks) / job.totalTicks), 4), brush_.Get());
            }
            if (!b.production.empty() && b.production.front().remainingTicks == 0)
                text(L"Освободите выход из здания", rect(info.x, info.y + 145, info.width, 23), 0xe4c388);
        } else text(L"Расширяет обзор днём и ночью", rect(info.x, info.y + 76, info.width, 30), 0x9eb6b7);
    } else if (unit) {
        text(ui.selection.ids.size() == 1 ? wide(unit->definition.displayName) : L"Группа  /  " + std::to_wstring(ui.selection.ids.size()),
            rect(info.x, info.y, info.width, 34), 0xe0eade, true);
        const bool single = ui.selection.ids.size() == 1;
        if (single)
            text(L"Ур. " + std::to_wstring(unit->level()) + L"   Здоровье " + std::to_wstring(unit->health) + L" / " + std::to_wstring(unit->maximumHealth()) +
                (unit->definition.isWorker() ? L"   Груз " + std::to_wstring(unit->cargo) : L"   Урон " + std::to_wstring(unit->attackDamage())),
                rect(info.x, info.y + 39, info.width, 24), 0x9dc1b6);
        else text(L"ПКМ — общий приказ   •   Shift — добавить", rect(info.x, info.y + 39, info.width, 24), 0x9dc1b6);
        const size_t maxCards = std::max(size_t{1}, static_cast<size_t>(info.width / 45));
        for (size_t i = 0; i < std::min(maxCards, ui.selection.ids.size()); ++i) {
            const auto* member = game.unit(ui.selection.ids[i]);
            if (member && UiRect{info.x + i * 45.0f, info.y + 81, 42, 42}.contains(ui.mouse)) tooltip = &member->definition;
            panel({info.x + i * 45.0f, info.y + 81, 42, 42}, member && member->hero ? 0x6c603d : 0x244039);
            if (member) unitPortrait(member->definition.sprite, {info.x + i * 45.0f - 5, info.y + 78}, {48,48}, teamColor_);
            buttonFrame({info.x + i * 45.0f, info.y + 81, 42, 42});
        }
        if (single && unit->hero) {
            const auto experience = unit->atMaxLevel() ? L"Максимальный уровень" :
                L"Опыт  " + std::to_wstring(unit->experienceInLevel()) + L" / " + std::to_wstring(unit->experienceToLevel());
            text(experience, rect(info.x + 58, info.y + 77, info.width - 58, 26), 0xc8b9f2);
            panel({info.x + 58, info.y + 110, info.width - 70, 8}, 0x26313e);
            panel({info.x + 58, info.y + 110, (info.width - 70) * unit->experienceFraction(), 8}, 0xaca0ec);
        }
        const bool builder = std::any_of(ui.selection.ids.begin(), ui.selection.ids.end(), [&](EntityId id) { const auto* u = game.unit(id); return u && u->definition.canBuild; });
        if (builder) {
            const size_t count = std::min(size_t{3}, game.buildingTypes().size());
            const std::array<std::wstring, 3> keys{L"H", L"B", L"O"};
            for (size_t i = 0; i < count; ++i) {
                const auto& type = *game.buildingTypes()[i];
                commandTypes[i] = &type;
                labels[i] = keys[i] + L"   " + (type.visual == EntityVisual::Tower ? L"Башня" : wide(type.displayName));
                details[i] = std::to_wstring(type.cost.crystals) + L" кр.";
            }
        }
        labels[3] = L"Стоп"; details[3] = L"Ctrl + S";
        text(L"F1 — армия   •   F2 — герой   •   ПКМ — приказ", rect(info.x, info.y + 143, info.width, 23), 0x6e9395);
    } else {
        text(L"Нет выделения", rect(info.x, info.y, info.width, 34), 0xe0eade, true);
        text(L"Выберите здание или выделите юнитов рамкой.", rect(info.x, info.y + 47, info.width, 45), 0x9eb6b7);
        text(L"WASD — камера   •   Колесо — масштаб\nG — сетка   •   Пробел — пауза", rect(info.x, info.y + 104, info.width, 55), 0x6e9395);
    }
    for (size_t i = 0; i < layout.commands.size(); ++i) {
        const auto b = layout.commands[i];
        if (b.contains(ui.mouse) && commandTypes[i]) tooltip = commandTypes[i];
        panel(b, labels[i].empty() ? 0x142630 : b.contains(ui.mouse) ? 0x36564f : 0x233d3d);
        if (!labels[i].empty()) {
            text(labels[i], rect(b.x + 6, b.y + 8, b.width - 12, 40), 0xdde8d9);
            text(details[i], rect(b.x + 9, b.y + 49, b.width - 18, 33), 0x91afa9);
        }
        buttonFrame(b);
    }
    if (tooltip) {
        const float width = std::min(420.0f, extent.x - 24), height = 224;
        const float x = std::clamp(ui.mouse.x + 20, 12.0f, extent.x - width - 12);
        const float y = std::clamp(ui.mouse.y - height - 12, 64.0f, extent.y - height - 12);
        panel({x - 1, y - 1, width + 2, height + 2}, 0x677a6c);
        panel({x, y, width, height}, 0x17272e);
        text(wide(tooltip->displayName), rect(x + 14, y + 10, width - 28, 36), 0xe9e1c5, true);
        text(wide(tooltip->factionName), rect(x + 14, y + 46, width - 28, 24), 0xa8b9b0);
        text(wide(tooltip->description), rect(x + 14, y + 75, width - 28, 77), 0xcbd7d0);
        const auto& cost = tooltip->cost;
        text(L"Кристаллы: " + std::to_wstring(cost.crystals),
            rect(x + 14, y + 165, width - 28, 23), game.storedCrystals() >= cost.crystals ? 0xcbb5ef : 0xed8b80);
        text(L"Лимит армии: " + std::to_wstring(cost.supply),
            rect(x + 14, y + 193, width - 28, 23), game.armySupply().canReserve(cost.supply) ? 0x9dd7b0 : 0xed8b80);
    }
}
}
