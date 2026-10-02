#include "RenderSupport.hpp"
#include <iomanip>
#include <sstream>

namespace rts {
using namespace render;
void Renderer::hud(const Simulation& game, const GameplayUi& ui, bool paused, const WorldView& view, bool grid) {
    const auto extent = size();
    const Building* building = ui.selection.ids.size() == 1 ? game.building(ui.selection.ids.front()) : nullptr;
    const BattleLayout layout(extent);
    const auto panel = [&](UiRect area, unsigned color) {
        brush_->SetColor(D2D1::ColorF(color));
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect(area.x, area.y, area.width, area.height), 6, 6), brush_.Get());
    };
    const auto icon = [&](const char* id, Vec2 position, float size) {
        const auto found = commandIcons_.find(id);
        if (found == commandIcons_.end()) return;
        auto& resource = found->second;
        const auto color = resource.definition.mask.empty() ? 0 : teamColor_;
        if (!resource.bitmap || resource.color != color) {
            loadBitmap(resource.definition.image, resource.bitmap, color, SpriteTeamMask::None, resource.definition.mask);
            resource.color = color;
        }
        const auto dimensions = resource.bitmap->GetPixelSize();
        const float scale = size / std::max(dimensions.width, dimensions.height);
        const Vec2 extent{dimensions.width * scale, dimensions.height * scale};
        sprite(resource.bitmap.Get(), rect(0, 0, float(dimensions.width), float(dimensions.height)),
            position + (Vec2{size, size} - extent) * .5f, extent);
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
    const bool compact = extent.x < 1060;
    text(paused ? L"ПАУЗА  /  Пробел" : clockText.str(), rect(145, 20, compact ? 180.0f : 220.0f, 25), 0xaabfbd);
    if (!compact) text(grid ? L"F3 — сетка: вкл." : L"F3 — сетка: выкл.", rect(360, 21, 160, 25), 0x7d9b99);
    text(L"КРИСТАЛЛЫ  " + std::to_wstring(game.storedCrystals()), rect(extent.x - 440, compact ? 21.0f : 16.0f, 230, 35), 0xccb3f4, !compact);
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
    const auto idleCount = std::count_if(game.units().begin(), game.units().end(), [&](const Unit& u) { return idleWorker(game, u); });
    panel(layout.idleWorker, idleCount == 0 ? 0x202b31 : layout.idleWorker.contains(ui.mouse) ? 0x36564f : 0x233d3d);
    worldOpacity_ = idleCount ? 1.0f : .3f;
    icon("idle-worker", {layout.idleWorker.x, layout.idleWorker.y}, layout.idleWorker.width);
    worldOpacity_ = 1;
    buttonFrame(layout.idleWorker);
    panel({12, extent.y - 238, std::min(800.0f, extent.x - 24), 28}, 0x15262d);
    const std::wstring hint = !ui.placement.empty() ? L"Размещение: " + wide(game.entityType(ui.placement).displayName) + L"  •  ЛКМ — строить  •  ПКМ / Esc — отмена" :
        ui.rallyMode ? L"ЛКМ по карте — точка сбора. ПКМ / Esc — отмена." :
        ui.orderMode ? std::wstring(orderName(*ui.orderMode)) + L"  •  ЛКМ — цель  •  ПКМ / Esc — отмена" :
        ui.buildMenu ? L"Выберите постройку. X / Esc — назад." : game.message();
    text(hint, rect(24, extent.y - 233, extent.x - 48, 25), 0xb8cbc5);
    drawMinimap(game, layout, view);
    const auto info = layout.info;
    const Unit* unit = ui.selection.activeUnit(game);
    const Crystal* resource = ui.selection.ids.size() == 1 ? game.crystal(ui.selection.ids.front()) : nullptr;
    if (resource && (!game.fog().visible(resource->cell) || resource->remaining <= 0)) resource = nullptr;
    const EntityDefinition* tooltip = hero && heroArea.contains(ui.mouse) ? &hero->definition : nullptr;
    std::wstring actionTitle, actionDescription;
    wchar_t tooltipKey{};
    std::array<const EntityDefinition*, commandSlots> commandTypes{};
    if (building && info.contains(ui.mouse)) tooltip = &building->definition;
    std::array<std::wstring, commandSlots> labels{}, details{};
    std::array<bool, commandSlots> enabled{}, active{};
    std::array<const char*, commandSlots> icons{};
    std::array<wchar_t, commandSlots> buttonKeys{};
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
            labels[5] = L"Отменить строительство"; details[5] = L"Прекратить строительство и вернуть ресурсы.";
            icons[5] = "cancel"; buttonKeys[5] = L'X';
        } else if (!b.definition.trainableUnits.empty()) {
            constexpr std::array keys{L'Q', L'E', L'T'};
            for (size_t choice = 0; choice < b.definition.trainableUnits.size(); ++choice) {
                const auto slot = choice * 2;
                const auto& type = game.entityType(b.definition.trainableUnits[choice]);
                commandTypes[slot] = &type;
                labels[slot] = wide(type.displayName); buttonKeys[slot] = keys[choice];
            }
            labels[1] = L"Точка сбора"; details[1] = L"Выбрать точку сбора. Её также можно задать ПКМ по карте.";
            icons[1] = "rally"; buttonKeys[1] = L'R'; active[1] = ui.rallyMode;
            if (!b.production.empty()) {
                labels[5] = L"Отменить обучение"; details[5] = L"Отменить последний заказ в очереди и вернуть ресурсы.";
                icons[5] = "cancel"; buttonKeys[5] = L'X';
            }
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
        const SelectionCards portraits(game, ui.selection, info);
        const auto group = ui.selection.activeGroup(game);
        drawUnitSelection(game, ui.selection, info);
        if (SelectionPanelLayout(info).portrait.contains(ui.mouse)) tooltip = &unit->definition;
        for (const auto& card : portraits.cards) {
            const auto* member = game.unit(card.id);
            if (card.bounds.contains(ui.mouse) || card.healthBar().contains(ui.mouse)) tooltip = &member->definition;
        }
        const bool builder = commandEnabled(game, ui, UnitCommand::Build);
        if (builder && ui.buildMenu) {
            const size_t count = std::min(size_t{3}, game.buildingTypes().size());
            constexpr std::array keys{L'H', L'B', L'O'};
            for (size_t i = 0; i < count; ++i) {
                const auto& type = *game.buildingTypes()[i];
                commandTypes[i] = &type;
                labels[i] = wide(type.displayName); buttonKeys[i] = keys[i];
            }
        }
        const auto current = unitOrder(*unit);
        const bool sameOrder = std::all_of(group.ids.begin(), group.ids.end(), [&](EntityId id) {
            const auto* member = game.unit(id); return member && unitOrder(*member) == current;
        });
        if (ui.buildMenu) {
            labels[backCommandSlot] = L"Назад"; details[backCommandSlot] = L"Вернуться к приказам юнита.";
            icons[backCommandSlot] = "cancel"; buttonKeys[backCommandSlot] = L'X';
        } else for (const auto& command : unitCommands) {
            const size_t i = command.slot;
            if (!unitCommandVisible(game, ui, i)) continue;
            labels[i] = command.label; details[i] = command.description; buttonKeys[i] = command.key; icons[i] = command.icon;
            if (command.command != UnitCommand::Back) details[i] += command.scope == CommandScope::ActiveGroup ?
                L"\nДля активной группы." : L"\nДля всего выделения.";
            enabled[i] = true;
            const auto shown = ui.orderMode.value_or(current);
            active[i] = command.command != UnitCommand::Back && (ui.orderMode || sameOrder) &&
                (shown == command.order || (shown == OrderKind::Attack && command.command == UnitCommand::Attack));
        }
    } else {
        text(L"Нет выделения", rect(info.x, info.y, info.width, 34), 0xe0eade, true);
        text(L"Выберите здание или выделите юнитов рамкой.", rect(info.x, info.y + 47, info.width, 45), 0x9eb6b7);
        text(L"Края экрана / стрелки / средняя кнопка — камера\nF3 — сетка   •   Пробел — пауза", rect(info.x, info.y + 104, info.width, 55), 0x6e9395);
    }
    for (size_t i = 0; i < layout.commandCount; ++i) {
        const auto b = layout.commands[i];
        if (building || ui.buildMenu) enabled[i] = !labels[i].empty();
        if (b.contains(ui.mouse) && !labels[i].empty()) {
            if (commandTypes[i]) { tooltip = commandTypes[i]; tooltipKey = buttonKeys[i]; }
            else {
                actionTitle = labels[i] + L"  [" + buttonKeys[i] + L"]";
                actionDescription = details[i];
            }
        }
        panel(b, !enabled[i] ? 0x142630 : active[i] ? 0x536749 : b.contains(ui.mouse) ? 0x36564f : 0x233d3d);
        if (!labels[i].empty()) {
            worldOpacity_ = enabled[i] ? 1.0f : .3f;
            if (icons[i]) {
                icon(icons[i], {b.x, b.y}, b.width);
            } else if (const auto* type = commandTypes[i]) {
                target_->PushAxisAlignedClip(rect(b.x, b.y, b.width, b.height), D2D1_ANTIALIAS_MODE_ALIASED);
                if (type->mobile && !type->sprite.image.empty()) {
                    unitPortrait(type->sprite, {b.x, b.y}, {b.width, b.height}, teamColor_);
                } else if (const auto* stage = type->buildingSprite.stage(type->constructionTicks, type->constructionTicks)) {
                    const float scale = std::min(b.width / stage->source[2], b.height / stage->source[3]);
                    const float width = stage->source[2] * scale, height = stage->source[3] * scale;
                    buildingImage(*stage, {b.x + (b.width - width) * .5f, b.y + (b.height - height) * .5f, width, height}, teamColor_);
                } else icon("build", {b.x, b.y}, b.width);
                target_->PopAxisAlignedClip();
            }
            worldOpacity_ = 1;
        }
        buttonFrame(b);
        if (active[i] && enabled[i]) {
            brush_->SetColor(D2D1::ColorF(0xe5cd83));
            target_->DrawRectangle(rect(b.x + 1, b.y + 1, b.width - 2, b.height - 2), brush_.Get(), 2);
        }
    }
    for (size_t i = 0; i < controlGroupCount; ++i) {
        const auto& members = ui.controlGroups.members(i);
        if (members.empty()) continue;
        const auto b = layout.controlGroups[i];
        const bool hover = b.contains(ui.mouse), selected = ui.controlGroups.selected(i, ui.selection);
        panel(b, selected ? 0x465541 : hover ? 0x36564f : 0x233d3d);
        brush_->SetColor(D2D1::ColorF(selected ? 0xe5cd83 : 0x526c68));
        target_->DrawRectangle(rect(b.x, b.y, b.width, b.height), brush_.Get());
        text(std::wstring(1, controlGroupKey(i)), rect(b.x + 6, b.y + 6, 12, 22), 0xe5cd83);
        text(std::to_wstring(members.size()), rect(b.x + 21, b.y + 6, 26, 22), 0xcbd7d0);
        if (hover) {
            actionTitle = L"Группа " + std::wstring(1, controlGroupKey(i));
            actionDescription = L"Юнитов: " + std::to_wstring(members.size()) +
                L"\nНажмите цифру или кнопку, чтобы выделить группу.\nCtrl + цифра — записать текущее выделение.";
        }
    }
    if (layout.idleWorker.contains(ui.mouse)) {
        actionTitle = L"Незанятый работник  [F8]";
        actionDescription = L"Свободно: " + std::to_wstring(idleCount) +
            L"\nВыбрать следующего незанятого работника и переместить к нему камеру.";
    }
    if (!actionTitle.empty()) {
        const float width = std::min(380.0f, extent.x - 24), height = 132;
        const float x = std::clamp(ui.mouse.x - width, 12.0f, extent.x - width - 12);
        const float y = std::clamp(ui.mouse.y - height - 12, 64.0f, extent.y - height - 12);
        panel({x, y, width, height}, 0x17272e);
        text(actionTitle, rect(x + 12, y + 8, width - 24, 30), 0xe9e1c5, true);
        text(actionDescription, rect(x + 12, y + 44, width - 24, 80), 0xcbd7d0);
    } else if (tooltip) {
        const float width = std::min(420.0f, extent.x - 24), height = 224;
        const float x = std::clamp(ui.mouse.x + 20, 12.0f, extent.x - width - 12);
        const float y = std::clamp(ui.mouse.y - height - 12, 64.0f, extent.y - height - 12);
        panel({x - 1, y - 1, width + 2, height + 2}, 0x677a6c);
        panel({x, y, width, height}, 0x17272e);
        const auto title = wide(tooltip->displayName) + (tooltipKey ? L"  [" + std::wstring(1, tooltipKey) + L"]" : L"");
        text(title, rect(x + 14, y + 10, width - 28, 36), 0xe9e1c5, true);
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
