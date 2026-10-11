#include "RenderSupport.hpp"
#include "rts/Library.hpp"
#include <iomanip>
#include <sstream>

namespace rts {
using namespace render;
namespace {
std::wstring amount(float value) {
    std::wostringstream result;
    result << std::fixed << std::setprecision(value == std::floor(value) ? 0 : 1) << value;
    return result.str();
}
}
void Renderer::drawLibrary(const MenuState& menu, const Definitions& definitions, Vec2 mouse) {
    // Browsing can introduce graphics from races outside the current match.
    residentMatchAssets_.reset();
    const LibraryLayout layout(size());
    mouse = layout.local(mouse);
    D2D1_MATRIX_3X2_F previous;
    target_->GetTransform(&previous);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(layout.scale, layout.scale) *
        D2D1::Matrix3x2F::Translation(layout.origin.x, layout.origin.y) * previous);
    worldOpacity_ = 1;
    // Keep only the current preview tint instead of retaining every full-size color variant.
    const auto color = teamRgb(menu.player.color);
    std::erase_if(maskedImages_, [color, this](const auto& entry) {
        const bool remove = !std::get<1>(entry.first).empty() && std::get<2>(entry.first) != color;
        if (remove) spriteLights_.erase(entry.second.Get());
        return remove;
    });
    const auto fill = [&](UiRect area, unsigned color, float opacity = 1) {
        brush_->SetColor(D2D1::ColorF(color, opacity));
        target_->FillRectangle(rect(area.x, area.y, area.width, area.height), brush_.Get());
    };
    const auto choice = [&](UiRect area, const std::wstring& caption, bool selected, const EntityDefinition* entry = nullptr) {
        fill(area, selected ? 0x514432 : (area.contains(mouse) ? 0x343836 : 0x1c2729), .97f);
        if (selected) fill({area.x, area.y, 3, area.height}, 0xd6b476);
        if (entry) {
            const auto& icon = entry->constructible ? entry->buildingSprite.icon : entry->sprite.icon;
            const auto& mask = entry->constructible ? entry->buildingSprite.iconMask : entry->sprite.iconMask;
            const UiRect iconBounds{area.x + 8, area.y + 4, 32, 32};
            if (!icon.empty()) uiImage(icon, mask, iconBounds, color);
            else centeredText(L"?", iconBounds, 0xbac5bb);
            const auto alignment = bodyFormat_->GetParagraphAlignment();
            bodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            text(caption, rect(area.x + 48, area.y + 2, area.width - 60, area.height - 4), selected ? 0xf2ddb5 : 0xbac5bb);
            bodyFormat_->SetParagraphAlignment(alignment);
            return;
        }
        text(caption, rect(area.x + 14, area.y + 10, area.width - 28, area.height - 14), selected ? 0xf2ddb5 : 0xbac5bb);
    };
    text(L"ЛИБРАРИЙ", rect(34, 28, 550, 40), 0xf1ddb5, true);
    text(L"Книга народов, воинов и святилищ", rect(36, 70, 650, 24), 0xb4b8aa);
    text(L"Цвет команды", rect(824, 52, 140, 24), 0xb4b8aa);
    line({34, 108}, {258, 108}, 0x806d4e);
    text(L"РАСЫ", rect(48, 139, 200, 24), 0xc5b088);
    text(L"ЗАПИСИ", rect(48, 329, 200, 24), 0xc5b088);
    choice(layout.back, L"‹  Назад   /   Esc", false);

    const auto factions = libraryFactions(definitions);
    for (size_t i = 0; i < LibraryLayout::factionRows && menu.libraryFactionScroll + i < factions.size(); ++i)
        choice(layout.factionRow(i), wide(factions[menu.libraryFactionScroll + i]->displayName), menu.libraryFaction == menu.libraryFactionScroll + i);
    fill({288, 124, 968, 602}, 0x000000, .4f);
    fill({278, 110, 970, 610}, 0x513526);
    fill({285, 114, 956, 602}, 0xa98c59);
    fill({292, 120, 464, 588}, 0xe6d9b7);
    fill({756, 120, 484, 588}, 0xf1e5c9);
    // Narrow translucent bands suggest the binding without introducing another art asset.
    for (int i = 0; i < 12; ++i) {
        fill({744.0f + i, 120, 1, 588}, 0x4a3225, .025f * i);
        fill({756.0f + i, 120, 1, 588}, 0x4a3225, .025f * (12 - i));
    }
    line({314, 162}, {724, 162}, 0xb6a078);
    line({782, 162}, {1216, 162}, 0xb6a078);
    line({314, 665}, {724, 665}, 0xb6a078);
    line({782, 665}, {1216, 665}, 0xb6a078);
    if (factions.empty()) {
        text(L"Книга пока пуста", rect(360, 260, 800, 44), 0x574732, true);
        drawColorSelect(ColorSelectLayout(layout.color, 800), menu, mouse);
        target_->SetTransform(previous);
        return;
    }
    const auto& faction = *factions.at(std::min(menu.libraryFaction, factions.size() - 1));
    for (size_t i = 0; i < libraryTabs.size(); ++i) {
        const auto count = libraryEntries(definitions, faction.id, libraryTabs[i]).size();
        choice(layout.tab(i), std::wstring(libraryTabNames[i]) + L" (" + std::to_wstring(count) + L")", menu.libraryTab == libraryTabs[i]);
    }
    const auto entries = libraryEntries(definitions, faction.id, menu.libraryTab);
    if (entries.empty()) {
        text(L"В этом разделе пока нет записей", rect(360, 260, 800, 44), 0x574732, true);
        drawColorSelect(ColorSelectLayout(layout.color, 800), menu, mouse);
        target_->SetTransform(previous);
        return;
    }
    const auto entryIndex = std::min(menu.libraryEntry, entries.size() - 1);
    const auto& entity = *entries[entryIndex];
    for (size_t i = 0; i < LibraryLayout::entryRows && menu.libraryEntryScroll + i < entries.size(); ++i) {
        const auto* entry = entries[menu.libraryEntryScroll + i];
        choice(layout.entryRow(i), wide(entry->displayName), entryIndex == menu.libraryEntryScroll + i, entry);
    }
    if (entries.size() > LibraryLayout::entryRows || factions.size() > LibraryLayout::factionRows)
        text(L"Колесо мыши — листать список", rect(42, 680, 224, 35), 0xb4b8aa);
    text(wide(entity.displayName), rect(782, 183, 432, 46), 0x3b3026, true);
    text(wide(entity.description), rect(782, 240, 424, 81), 0x5c4d3b);

    const UiRect art{320, 180, 396, 446};
    target_->PushAxisAlignedClip(rect(art.x, art.y, art.width, art.height), D2D1_ANTIALIAS_MODE_ALIASED);
    const auto& portrait = entity.constructible ? entity.buildingSprite.portrait : entity.sprite.portrait;
    const auto& portraitMask = entity.constructible ? entity.buildingSprite.portraitMask : entity.sprite.portraitMask;
    if (!portrait.empty()) {
        uiImage(portrait, portraitMask, art, color);
    } else {
        centeredText(L"Портрет готовится", art, 0x6f583b);
    }
    target_->PopAxisAlignedClip();

    std::vector<std::pair<std::wstring, std::wstring>> stats{
        {L"Здоровье", std::to_wstring(entity.maximumHealth)},
        {L"Стоимость", std::to_wstring(entity.cost.crystals)},
        {L"Лимит армии", std::to_wstring(entity.cost.supply)},
        {L"Атака", entity.attackDamage > 0 ? std::to_wstring(entity.attackDamage) : L"Не атакует"},
        {L"Обзор днём / ночью", std::to_wstring(entity.dayVision) + L" / " + std::to_wstring(entity.nightVision) + L" клеток"}
    };
    if (entity.constructible) {
        stats.emplace_back(L"Площадь", std::to_wstring(entity.width) + L" × " + std::to_wstring(entity.height) + L" клетки");
        stats.emplace_back(L"Строительство", amount(float(entity.constructionTicks) / Simulation::ticksPerSecond) + L" с");
    } else {
        stats.emplace_back(L"Обучение", amount(float(entity.trainingTicks) / Simulation::ticksPerSecond) + L" с");
        stats.emplace_back(L"Скорость", amount(entity.movementPerSecond) + L" клеток/с");
    }
    float y = 338;
    size_t statIndex = 0;
    for (const auto& [label, value] : stats) {
        text(label, rect(782, y, 208, 25), 0x76634a);
        const char* resource = statIndex == 1 ? "crystal" : statIndex == 2 ? "supply" : nullptr;
        if (resource) drawResourceIcon(resource, {992, y, 24, 24});
        text(value, rect(resource ? 1022.0f : 992.0f, y, resource ? 192.0f : 222.0f, 25), 0x392f25);
        line({782, y + 25}, {1214, y + 25}, 0xd6c7a6);
        y += 31;
        ++statIndex;
    }
    std::wstring abilities;
    if (entity.acceptsCargo) abilities = L"Принимает кристаллы рабочих.\n";
    if (!entity.trainableUnits.empty()) {
        abilities += L"Обучает: ";
        for (size_t i = 0; i < entity.trainableUnits.size(); ++i) {
            if (i) abilities += L", ";
            const auto& trainee = definitions.entity(entity.trainableUnits[i]);
            abilities += wide(trainee.displayName) + L" (" + amount(float(trainee.trainingTicks) / Simulation::ticksPerSecond) + L" с)";
        }
    }
    if (entity.canBuild) abilities += L" Возводит здания.";
    if (entity.gatherAbility) abilities += L"\n" + wide(entity.gatherAbility->displayName) + L": " + wide(entity.gatherAbility->description);
    text(abilities, rect(782, 575, 428, 78), 0x635239);
    text(std::to_wstring(entryIndex + 1) + L" / " + std::to_wstring(entries.size()), rect(1150, 679, 80, 22), 0x8a7455);
    std::wstring commander;
    for (const auto& c : definitions.commanders()) if (c.factionId == faction.id) {
        if (!commander.empty()) commander += L", ";
        commander += wide(c.displayName);
    }
    text(L"Командир: " + commander, rect(294, 742, 890, 26), 0xccb68d);
    drawColorSelect(ColorSelectLayout(layout.color, 800), menu, mouse);
    target_->SetTransform(previous);
}
}
