#include "RenderSupport.hpp"
#include "rts/Library.hpp"
#include <iomanip>
#include <sstream>

namespace rts {
using namespace render;
namespace {
std::wstring amount(float value) {
    std::wostringstream out;
    out << std::fixed << std::setprecision(value == std::floor(value) ? 0 : 1) << value;
    auto result = out.str();
    std::replace(result.begin(), result.end(), L'.', L',');
    return result;
}
}

void Renderer::drawUnitStats(const Unit& unit, UiRect b, bool showExtra) {
    const auto& d = unit.definition;
    const float width = std::min(b.width, 560.0f);
    text(wide(d.displayName), rect(b.x, b.y, b.width, 32), 0xe0eade, width >= 300);
    const SelectionStatsLayout stats(b);
    for (const auto area : {stats.attack, stats.armor}) {
        brush_->SetColor(D2D1::ColorF(0x20343c));
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect(area.x, area.y, area.width, area.height), 4, 4), brush_.Get());
    }
    const auto a = stats.attack;
    const unsigned attackColor = unit.attackDamage() > 0 ? 0xe4d4a5 : 0x718482;
    // Simple sword and shield shapes stay sharp at any UI scale.
    const std::array<Vec2, 4> blade{{{a.x + 12, a.y + 17}, {a.x + 20, a.y + 5},
        {a.x + 21, a.y + 11}, {a.x + 15, a.y + 19}}};
    polygon(blade, attackColor);
    line({a.x + 8, a.y + 17}, {a.x + 17, a.y + 23}, attackColor, 2);
    line({a.x + 12, a.y + 20}, {a.x + 8, a.y + 25}, attackColor, 3);
    const auto s = stats.armor;
    const std::array<Vec2, 5> shield{{{s.x + 7, s.y + 6}, {s.x + 23, s.y + 6},
        {s.x + 22, s.y + 17}, {s.x + 15, s.y + 24}, {s.x + 8, s.y + 17}}};
    polygon(shield, 0x9bc4d4, 1, false);
    line({s.x + 15, s.y + 8}, {s.x + 15, s.y + 20}, 0x9bc4d4);
    text(unit.attackDamage() > 0 ? std::to_wstring(unit.attackDamage()) : L"—",
        rect(a.x + 34, a.y + 3, a.width - 38, 22), attackColor);
    text(std::to_wstring(unit.armor()), rect(s.x + 34, s.y + 3, s.width - 38, 22), 0xcbdde4);
    text(d.mobile ? L"Скорость  " + amount(d.movementPerSecond) : L"Неподвижен",
        rect(stats.movement.x, stats.movement.y, stats.movement.width, stats.movement.height), 0xcbd7d0);
    std::wstring tags;
    for (const auto& id : d.tags.ids()) {
        if (!tags.empty()) tags += L", ";
        tags += wide(worldAssets_.tagCatalog().tag(id).displayName);
    }
    if (tags.empty()) tags = L"Нет тегов";
    ComPtr<IDWriteTextLayout> tagText;
    check(writeFactory_->CreateTextLayout(tags.c_str(), static_cast<UINT32>(tags.size()), bodyFormat_.Get(),
        stats.tags.width, stats.tags.height, tagText.GetAddressOf()));
    tagText->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<IDWriteInlineObject> ellipsis;
    check(writeFactory_->CreateEllipsisTrimmingSign(bodyFormat_.Get(), ellipsis.GetAddressOf()));
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    check(tagText->SetTrimming(&trimming, ellipsis.Get()));
    brush_->SetColor(D2D1::ColorF(0x9eb6b7));
    target_->DrawTextLayout({stats.tags.x, stats.tags.y}, tagText.Get(), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (!showExtra) return;
    std::wstring extra;
    const auto* selectedHarvest = unit.harvestAbility() ? unit.harvestAbility() : d.gatherAbility();
    if (unit.progression().hero) {
        extra = L"Уровень " + std::to_wstring(unit.level()) + (unit.atMaxLevel() ? L"  ·  Максимум" :
            L"  ·  Опыт " + std::to_wstring(unit.experienceInLevel()) + L" / " + std::to_wstring(unit.experienceToLevel()));
    } else if (selectedHarvest && selectedHarvest->harvest()->credit == HarvestCredit::Direct) {
        const auto harvest = *selectedHarvest->harvest();
        extra = L"Добыча  " + amount(float(harvest.amount) * Simulation::ticksPerSecond / harvest.intervalTicks) + L" кр/с";
    } else if (d.carryCapacity > 0) {
        extra = L"Кристаллы  " + std::to_wstring(unit.gathering().cargo) + L" / " + std::to_wstring(d.carryCapacity);
    } else extra = L"Уровень " + std::to_wstring(unit.level()) + L"  ·  Лимит " + std::to_wstring(d.cost.supply);
    const char* resourceIcon = unit.progression().hero ? nullptr : d.canGather() ? "crystal" : "supply";
    if (resourceIcon) drawResourceIcon(resourceIcon, {b.x, b.y + 137, 22, 22});
    const float extraInset = resourceIcon ? 28.f : 0.f;
    text(extra, rect(b.x + extraInset, b.y + 137, width - extraInset, 24), unit.progression().hero ? 0xd6c38a : 0xe4d4a5);
}

void Renderer::selectionStatsTooltip(const Unit& unit, UiRect bounds, Vec2 mouse, const ArmorRules& rules,
    std::wstring& title, std::wstring& description) {
    const SelectionStatsLayout stats(bounds);
    if (stats.attack.contains(mouse)) {
        title = L"Атака";
        description = wide(attackDescription(unit.definition, unit.attackDamage()));
    } else if (stats.armor.contains(mouse)) {
        title = L"Броня: " + std::to_wstring(unit.armor());
        description = wide(armorDescription(unit.armor(), rules));
    } else if (stats.movement.contains(mouse)) {
        title = L"Передвижение и обзор";
        description = unit.definition.mobile ? L"Скорость: " + amount(unit.definition.movementPerSecond) + L" ед./с" : L"Сущность не может передвигаться.";
        description += L"\nОбзор днём: " + std::to_wstring(unit.definition.dayVision) +
            L"\nОбзор ночью: " + std::to_wstring(unit.definition.nightVision);
    } else if (stats.tags.contains(mouse)) {
        title = L"Теги";
        description.clear();
        for (const auto& id : unit.definition.tags.ids()) {
            const auto& tag = worldAssets_.tagCatalog().tag(id);
            if (!description.empty()) description += L"\n";
            description += wide(tag.displayName);
            if (!tag.description.empty()) description += L" — " + wide(tag.description);
        }
        if (description.empty()) description = L"Теги не назначены.";
    }
}

void Renderer::drawUnitSelection(const Simulation& game, const Selection& selection, UiRect info) {
    const auto* unit = selection.inspectedUnit(game);
    if (!unit) return;
    const SelectionPanelLayout layout(info);
    const auto p = layout.portrait;
    const auto color = unit->owner == game.player().id ? teamColor_ : enemyColor_;
    drawPortraitBackdrop(p, color);
    target_->PushAxisAlignedClip(rect(p.x + 4, p.y + 4, p.width - 8, p.height - 8), D2D1_ANTIALIAS_MODE_ALIASED);
    unitHudPortrait(unit->definition.sprite, {p.x + 4, p.y + 4, p.width - 8, p.height - 8}, color);
    target_->PopAxisAlignedClip();
    buttonFrame(p);
    portraitVitals(layout, unit->vitals().health, unit->maximumHealth(), 0x9dd7b0, unit->vitals().mana, unit->maximumMana());
    const SelectionCards cards(game, selection, info);
    if (cards.total <= 1) {
        drawUnitStats(*unit, layout.content);
        return;
    }
    for (const auto& card : cards.cards) {
        const auto* member = game.unit(card.id);
        const auto b = card.bounds;
        brush_->SetColor(D2D1::ColorF(card.active ? 0x465541 : member->progression().hero ? 0x55492f : 0x244039));
        target_->FillRectangle(rect(b.x, b.y, b.width, b.height), brush_.Get());
        target_->PushAxisAlignedClip(rect(b.x, b.y, b.width, b.height), D2D1_ANTIALIAS_MODE_ALIASED);
        unitIcon(member->definition.sprite, b, teamColor_);
        target_->PopAxisAlignedClip();
        buttonFrame(b);
        if (card.active) {
            brush_->SetColor(D2D1::ColorF(0xe5cd83));
            target_->DrawRectangle(rect(b.x + 1, b.y + 1, b.width - 2, b.height - 2), brush_.Get(), 2);
        }
        drawHealthBar(*member, card.healthBar(), game.clock().elapsedTicks());
        drawManaBar(*member, card.manaBar());
    }
}
}
