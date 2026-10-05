#include "RenderSupport.hpp"
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

void Renderer::drawUnitStats(const Unit& unit, UiRect b) {
    const auto& d = unit.definition;
    const float width = std::min(b.width, 560.0f);
    const bool columns = width >= 360;
    text(wide(d.displayName), rect(b.x, b.y, b.width, 32), 0xe0eade, width >= 300);
    const std::wstring role = unit.hero ? L"Герой" : d.isWorker() ? L"Рабочий" : L"Воин";
    text(width >= 300 ? wide(d.factionName) + L"  ·  " + role : role,
        rect(b.x, b.y + 34, width, 24), 0x9eb6b7);
    const auto damage = unit.attackDamage() > 0 ? L"Атака  " + std::to_wstring(unit.attackDamage()) : L"Не атакует";
    const auto range = unit.attackDamage() > 0 ? L"Дальность  " + amount(d.attackRange) : L"";
    const auto speed = L"Скорость  " + amount(d.movementPerSecond) + L" кл/с";
    const auto vision = L"Обзор  " + std::to_wstring(d.dayVision) + L" / " + std::to_wstring(d.nightVision) + (width >= 300 ? L" (день / ночь)" : L"");
    if (columns) {
        const float second = b.x + width * .5f;
        text(damage, rect(b.x, b.y + 73, width * .5f - 8, 22), 0xd7ddd0);
        text(range, rect(b.x, b.y + 98, width * .5f - 8, 22), 0x9eb6b7);
        text(speed, rect(second, b.y + 73, width * .5f, 22), 0xd7ddd0);
        text(vision, rect(second, b.y + 98, width * .5f, 22), 0x9eb6b7);
    } else {
        text(damage + (range.empty() ? L"" : L"  ·  " + range), rect(b.x, b.y + 70, width, 21), 0xd7ddd0);
        text(speed, rect(b.x, b.y + 91, width, 21), 0x9eb6b7);
        text(vision, rect(b.x, b.y + 112, width, 21), 0x9eb6b7);
    }
    std::wstring extra;
    if (unit.hero) {
        extra = L"Уровень " + std::to_wstring(unit.level()) + (unit.atMaxLevel() ? L"  ·  Максимум" :
            L"  ·  Опыт " + std::to_wstring(unit.experienceInLevel()) + L" / " + std::to_wstring(unit.experienceToLevel()));
    } else if (d.carryCapacity > 0) {
        extra = L"Кристаллы  " + std::to_wstring(unit.cargo) + L" / " + std::to_wstring(d.carryCapacity);
    } else extra = L"Уровень " + std::to_wstring(unit.level()) + L"  ·  Лимит " + std::to_wstring(d.cost.supply);
    const char* resourceIcon = unit.hero ? nullptr : d.carryCapacity > 0 ? "crystal" : "supply";
    if (resourceIcon) drawResourceIcon(resourceIcon, {b.x, b.y + 137, 22, 22});
    const float extraInset = resourceIcon ? 28.f : 0.f;
    text(extra, rect(b.x + extraInset, b.y + 137, width - extraInset, 24), unit.hero ? 0xd6c38a : 0xe4d4a5);
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
    portraitVitals(layout, unit->health, unit->maximumHealth(), 0x9dd7b0, unit->mana, unit->maximumMana());
    const SelectionCards cards(game, selection, info);
    if (cards.total <= 1) {
        drawUnitStats(*unit, layout.content);
        return;
    }
    for (const auto& card : cards.cards) {
        const auto* member = game.unit(card.id);
        const auto b = card.bounds;
        brush_->SetColor(D2D1::ColorF(card.active ? 0x465541 : member->hero ? 0x55492f : 0x244039));
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
