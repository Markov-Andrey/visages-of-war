#include "rts/Library.hpp"
#include "RenderSupport.hpp"
#include <iomanip>
#include <sstream>

namespace rts {
using namespace render;
void Renderer::hud(const Simulation& game, const GameplayUi& ui, bool paused, const WorldView& view, bool grid) {
    const auto extent = size();
    const Building* building = ui.selection.ids.size() == 1 ? game.entity(ui.selection.ids.front()) : nullptr;
    if (building && building->definition.collision == CollisionShape::Circle && building->definition.trainableUnits.empty()) building = nullptr;
    if (building && !selectableEntity(game, building->id)) building = nullptr;
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
    // These are completed-frame intervals supplied by the game loop, not a
    // simulation tick rate or a timer around individual drawing commands.
    std::wostringstream frameText;
    if (frameFps_ > 0) frameText << std::fixed << std::setprecision(0) << L"FPS " << frameFps_
        << L"  |  " << std::setprecision(1) << frameMilliseconds_ << L" ms";
    else frameText << L"FPS --  |  -- ms";
    panel({extent.x - 198, 66, 180, 28}, 0x101c25);
    text(frameText.str(), rect(extent.x - 188, 70, 165, 22), 0xaabfbd);
    panel(layout.menu, layout.menu.contains(ui.mouse) ? 0x304851 : 0x1d303a);
    text(L"Меню / Esc", rect(29, 19, 94, 25), 0xd0ddd8);
    const int minutes = static_cast<int>(game.clock().minuteOfDay());
    std::wostringstream clockText;
    clockText << (game.clock().phase() == DayPhase::Day ? L"ДЕНЬ   " : L"НОЧЬ   ")
              << std::setfill(L'0') << std::setw(2) << minutes / 60 << L":" << std::setw(2) << minutes % 60;
    const bool compact = extent.x < 1060;
    text(paused ? L"ПАУЗА  /  Пробел" : clockText.str(), rect(145, 20, compact ? 180.0f : 220.0f, 25), 0xaabfbd);
    if (!compact) text(grid ? L"K — сетка: вкл." : L"K — сетка: выкл.", rect(360, 21, 160, 25), 0x7d9b99);
    const UiRect crystalCounter{extent.x - 440, 10, 230, 38}, supplyCounter{extent.x - 188, 10, 178, 38};
    drawResourceIcon("crystal", {crystalCounter.x, 12, 34, 34});
    text(std::to_wstring(game.storedCrystals()), rect(crystalCounter.x + 42, compact ? 21.0f : 16.0f, 178, 35), 0xeac2a4, !compact);
    drawResourceIcon("supply", {supplyCounter.x, 12, 34, 34});
    text(std::to_wstring(game.armySupply().used()) + L" / 100", rect(supplyCounter.x + 42, 21, 136, 27), 0xe4d4a5);
    const auto armyCount = std::count_if(game.units().begin(), game.units().end(), [&](const Unit& u) {
        return u.owner == game.player().id && !u.definition.isWorker();
    });
    panel(layout.army, layout.army.contains(ui.mouse) ? 0x36564f : 0x233d3d);
    text(L"F1  Армия  /  " + std::to_wstring(armyCount), rect(layout.army.x + 12, layout.army.y + 9, 144, 25), 0xdde8d9);
    const auto* hero = game.hero();
    const auto heroArea = layout.hero;
    panel(heroArea, !hero ? 0x303236 : heroArea.contains(ui.mouse) ? 0x535044 : 0x383d35);
    if (hero) {
        unitIcon(hero->definition.sprite, {heroArea.x, heroArea.y + 18, 58, 58}, teamColor_);
        text(L"F2  " + wide(hero->definition.displayName), rect(heroArea.x + 9, heroArea.y + 4, 147, 24), 0xebd693);
        text(L"Уровень " + std::to_wstring(hero->level()), rect(heroArea.x + 58, heroArea.y + 28, 102, 23), 0xe2dfc7);
        text(std::to_wstring((hero->health).rounded()) + L" / " + std::to_wstring((hero->maximumHealth()).rounded()), rect(heroArea.x + 58, heroArea.y + 50, 102, 23), 0x99d8ad);
        if (hero->maximumMana() > 0)
            text(std::to_wstring((hero->mana).rounded()) + L" / " + std::to_wstring((hero->maximumMana()).rounded()), rect(heroArea.x + 58, heroArea.y + 71, 102, 22), 0x8bbaff);
        panel({heroArea.x + 8, heroArea.y + 96, 148, 4}, 0x17222a);
        panel({heroArea.x + 8, heroArea.y + 96, 148 * hero->experienceFraction(), 4}, 0xaca0ec);
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
    const std::wstring hint = !ui.placement.empty() ? L"Размещение: " + wide(game.entityType(ui.placement).displayName) + L"  •  ЛКМ — строить  •  ПКМ / Esc — отмена" :
        ui.rallyMode ? L"ЛКМ по карте — точка сбора. ПКМ / Esc — отмена." :
        ui.orderMode ? std::wstring(orderName(*ui.orderMode)) + L"  •  ЛКМ — цель  •  ПКМ / Esc — отмена" :
        ui.buildMenu ? L"Выберите постройку. X / Esc — назад." : std::wstring(ui.notification.text(game));
    if (!hint.empty()) {
        const auto n = layout.notification;
        panel(n, 0x15262d);
        centeredText(hint, {n.x + 12, n.y + 6, n.width - 24, n.height - 12}, 0xb8cbc5);
    }
    drawMinimap(game, layout, view);
    const auto info = layout.info;
    const Unit* unit = ui.selection.inspectedUnit(game);
    const Crystal* resource = ui.selection.ids.size() == 1 ? game.crystal(ui.selection.ids.front()) : nullptr;
    if (resource && (!game.crystalVisible(*resource) || resource->remaining <= 0)) resource = nullptr;
    const EntityDefinition* tooltip = hero && heroArea.contains(ui.mouse) ? &hero->definition : nullptr;
    std::wstring actionTitle, actionDescription;
    wchar_t tooltipKey{};
    std::array<const EntityDefinition*, commandSlots> commandTypes{};
    std::array<const AbilityDefinition*, commandSlots> abilities{};
    if (building && SelectionPanelLayout(info).portrait.contains(ui.mouse)) tooltip = &building->definition;
    std::array<std::wstring, commandSlots> labels{}, details{};
    std::array<bool, commandSlots> enabled{}, active{};
    std::array<const char*, commandSlots> icons{};
    std::array<wchar_t, commandSlots> buttonKeys{};
    if (resource) {
        const SelectionPanelLayout portrait(info);
        drawCrystalPortrait(*resource, portrait);
        const auto content = portrait.content;
        text(wide(worldAssets_.crystalSprite(resource->definitionId).name), rect(content.x, content.y, content.width, 34), 0xe0eade, content.width >= 300);
        text(L"Нейтральный объект", rect(content.x, content.y + 39, content.width, 24), 0x9dc1b6);
        text(L"Кристальная жила.\nРабочие добывают кристаллы и доставляют их в ратушу.",
            rect(content.x, content.y + 78, content.width, 82), 0x7e9eaa);
    } else if (building) {
        const auto& b = *building;
        const SelectionPanelLayout portrait(info);
        if (b.definition.collision == CollisionShape::Footprint) drawBuildingPortrait(game, b, portrait);
        else {
            drawPortraitBackdrop(portrait.portrait, b.owner == game.player().id ? teamColor_ : enemyColor_);
            unitHudPortrait(b.definition.sprite, portrait.portrait, b.owner == game.player().id ? teamColor_ : enemyColor_);
            portraitVitals(portrait, b.health, b.maximumHealth(), 0x9dd7b0, b.mana, b.maximumMana());
        }
        const auto content = portrait.content;
        drawUnitStats(b, content, b.complete() && b.definition.trainableUnits.empty());
        if (!b.complete()) {
            text(L"Строительство  " + std::to_wstring(b.constructionProgress * 100 / b.definition.constructionTicks) + L"%", rect(content.x, content.y + 136, content.width, 28), 0xe4c388);
            labels[5] = L"Отменить строительство"; details[5] = L"Вернуть 50% стоимости в кристаллах и освободить лимит армии.";
            icons[5] = "cancel"; buttonKeys[5] = L'X';
        } else if (!b.definition.trainableUnits.empty()) {
            const bool blocked = !b.production.empty() && b.production.front().remainingTicks == 0;
            text(blocked ? L"Освободите выход из здания" : L"Очередь  " + std::to_wstring(b.production.size()) +
                L" / " + std::to_wstring(Building::productionQueueLimit),
                rect(content.x, content.y + 114, content.width, 22), blocked ? 0xe4c388 : 0x9eb6b7);
            const ProductionQueueLayout queue(content);
            for (size_t i = 0; i < b.production.size(); ++i) {
                const auto& job = b.production[i];
                const auto slot = queue.slots[i];
                const auto& type = game.entityType(job.definitionId);
                panel(slot, i == 0 ? 0x315b53 : 0x20363d);
                unitIcon(type.sprite, {slot.x + 2, slot.y + 2, slot.width - 4, slot.height - 4},
                    b.owner == game.player().id ? teamColor_ : enemyColor_);
                brush_->SetColor(D2D1::ColorF(i == 0 ? 0x88d2b6 : 0x526c68));
                target_->DrawRectangle(rect(slot.x, slot.y, slot.width, slot.height), brush_.Get());
                if (i == 0) {
                    const auto progress = rect(slot.x + 1, slot.y + slot.height - 5, slot.width - 2, 4);
                    brush_->SetColor(D2D1::ColorF(0x101c25));
                    target_->FillRectangle(progress, brush_.Get());
                    brush_->SetColor(D2D1::ColorF(0x88d2b6));
                    const float fraction = job.totalTicks > 0 ? 1 - float(job.remainingTicks) / job.totalTicks : 1;
                    target_->FillRectangle(rect(progress.left, progress.top, (slot.width - 2) * fraction, 4), brush_.Get());
                }
                if (slot.contains(ui.mouse)) {
                    if (b.owner == game.player().id) {
                        actionTitle = wide(type.displayName);
                        actionDescription = L"ЛКМ — отменить этот заказ.\nВозврат: " + std::to_wstring(cancellationRefund(job.paidCrystals)) +
                            L" кристаллов (50%).\nЛимит армии освобождается полностью.";
                    } else tooltip = &type;
                }
            }
        }
    } else if (unit) {
        const SelectionCards portraits(game, ui.selection, info);
        const auto group = ui.selection.activeGroup(game);
        drawUnitSelection(game, ui.selection, info);
        if (SelectionPanelLayout(info).portrait.contains(ui.mouse)) tooltip = &unit->definition;
        for (const auto& card : portraits.cards) {
            const auto* member = game.unit(card.id);
            if (card.bounds.contains(ui.mouse) || card.healthBar().contains(ui.mouse) ||
                (member->maximumMana() > 0 && card.manaBar().contains(ui.mouse))) tooltip = &member->definition;
        }
    } else {
        text(L"Нет выделения", rect(info.x, info.y, info.width, 34), 0xe0eade, true);
        text(L"Выберите здание или выделите юнитов рамкой.", rect(info.x, info.y + 47, info.width, 45), 0x9eb6b7);
        text(L"Края экрана / стрелки / средняя кнопка — камера\nK — сетка   •   Пробел — пауза", rect(info.x, info.y + 104, info.width, 55), 0x6e9395);
    }
    // Inspection shares stats and portraits, but never exposes another owner's command panel.
    if ((building && building->owner != game.player().id) || (unit && unit->owner != game.player().id)) {
        labels.fill({}); details.fill({}); icons.fill(nullptr); commandTypes.fill(nullptr);
        enabled.fill(false); active.fill(false); buttonKeys.fill(0);
    }
    const auto* caster = activeAbilityEntity(game, ui);
    if (caster) {
        // One action layout for every entity; menus only change the catalog choices.
        if (ui.buildMenu || ui.productionMenu) {
            labels.fill({}); details.fill({}); icons.fill(nullptr); commandTypes.fill(nullptr);
            constexpr std::array buildKeys{L'H', L'B', L'O'};
            constexpr std::array trainKeys{L'Q', L'E', L'T'};
            for (size_t i = 0; i < 3; ++i) {
                const auto slot = ui.buildMenu ? i : i * 2;
                if (const auto* type = catalogCommandAt(game, ui, slot)) {
                    commandTypes[slot] = type; labels[slot] = wide(type->displayName);
                    buttonKeys[slot] = ui.buildMenu ? buildKeys[i] : trainKeys[i]; enabled[slot] = true;
                }
            }
            if (ui.productionMenu) {
                labels[1] = L"Точка сбора"; icons[1] = "rally"; buttonKeys[1] = L'R'; enabled[1] = true;
                if (!caster->production.empty()) { labels[5] = L"Отменить заказ"; icons[5] = "cancel"; buttonKeys[5] = L'D'; enabled[5] = true; }
            }
            const auto pages = commandPageCount(game, ui);
            if (pages > 1) {
                const auto page = (ui.buildMenu ? ui.buildPage : ui.productionPage) % pages;
                labels[previousPageSlot] = L"Предыдущая"; buttonKeys[previousPageSlot] = L'J'; enabled[previousPageSlot] = true;
                labels[nextPageSlot] = L"Следующая"; buttonKeys[nextPageSlot] = L'N'; enabled[nextPageSlot] = true;
                details[previousPageSlot] = details[nextPageSlot] = L"Страница " + std::to_wstring(page + 1) + L" / " + std::to_wstring(pages);
            }
        } else {
            for (const auto& command : unitCommands) if (unitCommandVisible(game, ui, command.slot)) {
                const auto i = command.slot;
                labels[i] = command.label; details[i] = command.description; buttonKeys[i] = command.key; icons[i] = command.icon;
                enabled[i] = true; active[i] = command.command != UnitCommand::Back && ui.orderMode.value_or(unitOrder(*caster)) == command.order;
                if (command.command == UnitCommand::Gather && caster->definition.gatherAbility()) {
                    labels[i] = wide(caster->definition.gatherAbility()->displayName);
                    details[i] = wide(caster->definition.gatherAbility()->description);
                }
            }
            if (unitCommandVisible(game, ui, productionMenuSlot)) {
                labels[productionMenuSlot] = L"Производство"; buttonKeys[productionMenuSlot] = L'Y'; enabled[productionMenuSlot] = true;
                details[productionMenuSlot] = L"Открыть список производства и точку сбора.";
            }
            if (unitCommandVisible(game, ui, abilityPageSlot)) {
                const auto pages = (caster->definition.abilities.size() + abilityKeys.size() - 1) / abilityKeys.size();
                labels[abilityPageSlot] = L"Способности"; buttonKeys[abilityPageSlot] = L'F'; enabled[abilityPageSlot] = true;
                details[abilityPageSlot] = L"Следующая страница: " + std::to_wstring(ui.abilityPage % pages + 1) + L" / " + std::to_wstring(pages);
            }
        }
        if (unitCommandVisible(game, ui, backCommandSlot)) {
            labels[backCommandSlot] = L"Назад"; icons[backCommandSlot] = "cancel"; buttonKeys[backCommandSlot] = L'X'; enabled[backCommandSlot] = true;
        }
    }
    if (caster) for (size_t i = firstAbilitySlot; i < firstAbilitySlot + abilityKeys.size(); ++i) {
        const auto* ability = abilityCommandAt(game, ui, i);
        if (!ability) continue;
        abilities[i] = ability;
        labels[i] = wide(ability->displayName);
        details[i] = wide(ability->description) + L"\nМана: " + std::to_wstring((ability->manaCost).rounded()) +
            L"  ·  Перезарядка: " + std::to_wstring(ability->cooldownTicks / Simulation::ticksPerSecond) + L" с";
        buttonKeys[i] = abilityKey(game, ui, i - firstAbilitySlot);
        enabled[i] = caster->abilityCooldown(ability->id) == 0 && caster->mana >= ability->manaCost;
        active[i] = ui.orderMode == OrderKind::UseAbility && ui.pendingAbility == ability->id;
        details[i] += L"\nЦели: " + wide(abilityTargetDescription(ability->targeting)) + L".";
        if (ability->targeting != AbilityTargeting::None) details[i] += L"\n" + wide(abilityTargetDetails(*ability)) + L".";
        if (ability->rightClick) details[i] += L"\nПКМ по подходящей цели — применить.";
        for (const auto& effect : ability->effects) {
            details[i] += effect.recipient == EffectRecipient::Caster ? L"\nНа себя: " : L"\nНа цель: ";
            if (const auto* repair = std::get_if<RepairDefinition>(&effect.value))
                details[i] += L"ремонт, " + std::to_wstring(repair->constructionTimePercent) + L"% времени, " + std::to_wstring(repair->constructionCostPercent) + L"% цены.";
            else if (const auto* harvest = std::get_if<HarvestDefinition>(&effect.value))
                details[i] += L"добыча " + std::to_wstring(harvest->amount) + (harvest->credit == HarvestCredit::Direct ? L" крист., напрямую." : L" крист., в груз.");
            else if (const auto* restore = std::get_if<RestoreDefinition>(&effect.value))
                details[i] += L"+" + std::to_wstring(restore->amount.rounded()) + (restore->stat == RestoredStat::Health ? L" здоровья." : L" маны.");
        }
        if (ability->autocast) {
            const auto state = autocastState(game, ui, ability->id);
            std::wostringstream radius; radius << ability->autocast->radius;
            details[i] += L"\nАвтокаст: " + std::wstring(state == AutocastState::On ? L"включён" : state == AutocastState::Mixed ? L"у части группы" : L"выключен") +
                L". Радиус: " + radius.str() + L".\nПКМ — переключить для активной группы.\nЛКМ / " + buttonKeys[i] + L" — выбрать цель вручную.\nАвтоматически работает только при приказе «Стоп».";
        }
    }
    const auto commandPanel = layout.commandPanel;
    panel({commandPanel.x - 3, commandPanel.y - 3, commandPanel.width + 6, commandPanel.height + 6}, 0x31464c);
    panel(commandPanel, 0x101c25);
    for (size_t i = 0; i < layout.commandCount; ++i) {
        const auto b = layout.commands[i];
        if ((building || ui.buildMenu) && !abilities[i]) enabled[i] = !labels[i].empty();
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
            if (abilities[i]) {
                centeredText(wide(abilities[i]->displayName).substr(0, 1), {b.x + 22, b.y + 3, b.width - 22, std::min(32.f, b.height - 22)},
                    enabled[i] ? 0xb3d4ff : 0x5a718c, b.height >= 48);
            } else if (icons[i]) {
                icon(icons[i], {b.x, b.y}, b.width);
            } else if (const auto* type = commandTypes[i]) {
                target_->PushAxisAlignedClip(rect(b.x, b.y, b.width, b.height), D2D1_ANTIALIAS_MODE_ALIASED);
                if (type->collision == CollisionShape::Circle && !type->sprite.image.empty()) {
                    unitIcon(type->sprite, b, teamColor_);
                } else if (!type->buildingSprite.icon.empty()) {
                    uiImage(type->buildingSprite.icon, type->buildingSprite.iconMask, b, teamColor_);
                } else if (const auto* stage = type->buildingSprite.stage(type->constructionTicks, type->constructionTicks)) {
                    const float scale = std::min(b.width / stage->source[2], b.height / stage->source[3]);
                    const float width = stage->source[2] * scale, height = stage->source[3] * scale;
                    buildingImage(*stage, {b.x + (b.width - width) * .5f, b.y + (b.height - height) * .5f, width, height}, teamColor_);
                } else icon("build", {b.x, b.y}, b.width);
                target_->PopAxisAlignedClip();
            } else {
                std::wstring caption = i == previousPageSlot ? L"‹" : i == nextPageSlot ? L"›" : L"Выпуск";
                if (i == abilityPageSlot && caster) {
                    const auto pages = (caster->definition.abilities.size() + abilityKeys.size() - 1) / abilityKeys.size();
                    caption = std::to_wstring(ui.abilityPage % pages + 1) + L"/" + std::to_wstring(pages);
                }
                centeredText(caption, {b.x + 3, b.y + 18, b.width - 6, b.height - 21}, 0xb3d4cf);
            }
            worldOpacity_ = 1;
        }
        if (const auto* ability = abilities[i]) {
            const int cooldown = caster->abilityCooldown(ability->id);
            if (cooldown > 0) {
                brush_->SetColor(D2D1::ColorF(0x07101e, .8f));
                target_->FillRectangle(rect(b.x, b.y, b.width, b.height * cooldown / ability->cooldownTicks), brush_.Get());
                centeredText(std::to_wstring((cooldown + Simulation::ticksPerSecond - 1) / Simulation::ticksPerSecond),
                    {b.x + 22, b.y + 3, b.width - 22, b.height - 22}, 0xe2eaff, b.height >= 48);
            }
            if (ability->manaCost > 0) centeredText(std::to_wstring((ability->manaCost).rounded()), {b.x, b.y + b.height - 19, b.width, 19},
                caster->mana >= ability->manaCost ? 0x8bbaff : 0xed8b80);
        }
        buttonFrame(b);
        if (active[i] && enabled[i]) {
            brush_->SetColor(D2D1::ColorF(0xe5cd83));
            target_->DrawRectangle(rect(b.x + 1, b.y + 1, b.width - 2, b.height - 2), brush_.Get(), 2);
        }
    }
    for (size_t i = firstAbilitySlot; i < firstAbilitySlot + abilityKeys.size(); ++i)
        if (abilities[i] && abilities[i]->autocast) autocastFrame(layout.commands[i], autocastState(game, ui, abilities[i]->id));
    // Keep hotkeys above the button frames and cooldown overlays.
    for (size_t i = 0; i < layout.commandCount; ++i)
        if (!labels[i].empty() && buttonKeys[i]) commandHotkey(layout.commands[i], buttonKeys[i], enabled[i]);
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
    if (crystalCounter.contains(ui.mouse)) {
        actionTitle = L"Кристаллы";
        actionDescription = L"Добывается в жилах рабочими.";
    } else if (supplyCounter.contains(ui.mouse)) {
        actionTitle = L"Лимит армии";
        actionDescription = L"Занято: " + std::to_wstring(game.armySupply().used()) + L" / 100.\nКаждый юнит занимает часть лимита армии.";
    }
    const auto* inspected = building ? building : unit;
    if (inspected && ui.selection.ids.size() == 1) {
        selectionStatsTooltip(*inspected, SelectionPanelLayout(info).content, ui.mouse, game.armorRules(),
            actionTitle, actionDescription);
        const SelectionPanelLayout portrait(info);
        if (portrait.health.contains(ui.mouse)) {
            actionTitle = L"Здоровье";
            actionDescription = std::to_wstring((inspected->health).rounded()) + L" / " + std::to_wstring((inspected->maximumHealth()).rounded()) +
                L"\nРегенерация: " + wide(regenerationDescription(inspected->definition.healthRegenHundredths));
        } else if (inspected->maximumMana() > 0 && portrait.mana.contains(ui.mouse)) {
            actionTitle = L"Мана";
            actionDescription = std::to_wstring((inspected->mana).rounded()) + L" / " + std::to_wstring((inspected->maximumMana()).rounded()) +
                L"\nРегенерация: " + wide(regenerationDescription(inspected->definition.manaRegenHundredths));
        }
    }
    if (!actionTitle.empty()) {
        const float width = std::min(420.0f, extent.x - 24);
        ComPtr<IDWriteTextLayout> description;
        check(writeFactory_->CreateTextLayout(actionDescription.c_str(), static_cast<UINT32>(actionDescription.size()),
            bodyFormat_.Get(), width - 24, 10000, description.GetAddressOf()));
        DWRITE_TEXT_METRICS metrics{}; check(description->GetMetrics(&metrics));
        const float height = std::min(extent.y - 88, std::max(132.f, std::ceil(metrics.height) + 56));
        const float x = std::clamp(ui.mouse.x - width, 12.0f, extent.x - width - 12);
        const float y = std::clamp(ui.mouse.y - height - 12, 64.0f, extent.y - height - 12);
        panel({x, y, width, height}, 0x17272e);
        text(actionTitle, rect(x + 12, y + 8, width - 24, 30), 0xe9e1c5, true);
        text(actionDescription, rect(x + 12, y + 44, width - 24, height - 52), 0xcbd7d0);
    } else if (tooltip) {
        const float width = std::min(420.0f, extent.x - 24), height = 296;
        const float x = std::clamp(ui.mouse.x + 20, 12.0f, extent.x - width - 12);
        const float y = std::clamp(ui.mouse.y - height - 12, 64.0f, extent.y - height - 12);
        panel({x - 1, y - 1, width + 2, height + 2}, 0x677a6c);
        panel({x, y, width, height}, 0x17272e);
        const auto title = wide(tooltip->displayName) + (tooltipKey ? L"  [" + std::wstring(1, tooltipKey) + L"]" : L"");
        text(title, rect(x + 14, y + 10, width - 28, 36), 0xe9e1c5, true);
        text(wide(tooltip->factionName), rect(x + 14, y + 46, width - 28, 24), 0xa8b9b0);
        text(wide(tooltip->description), rect(x + 14, y + 75, width - 28, 77), 0xcbd7d0);
        const auto& cost = tooltip->cost;
        drawResourceIcon("crystal", {x + 14, y + 163, 24, 24});
        text(L"Стоимость: " + std::to_wstring(cost.crystals),
            rect(x + 44, y + 165, width - 58, 23), game.storedCrystals() >= cost.crystals ? 0xeac2a4 : 0xed8b80);
        drawResourceIcon("supply", {x + 14, y + 191, 24, 24});
        text(L"Лимит армии: " + std::to_wstring(cost.supply),
            rect(x + 44, y + 193, width - 58, 23), game.armySupply().canReserve(cost.supply) ? 0xe4d4a5 : 0xed8b80);
        text(L"Броня: " + std::to_wstring(tooltip->armor) + L"\n" + wide(armorDescription(tooltip->armor, game.armorRules())),
            rect(x + 14, y + 223, width - 28, 66), 0xcbd7d0);
    }
    if (ui.consoleOpen) {
        const float width = std::min(720.0f, extent.x - 40);
        const float y = layout.world.y + layout.world.height - 86;
        panel({20, y, width, 76}, 0x101c25);
        text(ui.consoleReply.empty() ? L"Консоль / Enter — выполнить, Esc — закрыть" : ui.consoleReply,
            rect(32, y + 9, width - 24, 25), 0xaac8be);
        text(L"> " + ui.consoleInput + L"|", rect(32, y + 40, width - 24, 30), 0xe5e8d7);
    }
}
}
