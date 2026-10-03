#include "RenderSupport.hpp"
#include "rts/Version.hpp"

namespace rts {
using namespace render;
void Renderer::drawMenu(const Simulation& game, const MenuState& menu, const Definitions& definitions, Vec2 mouse) {
    ensureTarget();
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0x0b161c));
    const auto extent = size();
    const MenuLayout layout(extent, menu.canResume);
    const auto backgroundSize = menuBackground_->GetSize();
    const auto foregroundSize = menuForeground_->GetSize();
    const MenuBackdropLayout backdrop(extent, {backgroundSize.width, backgroundSize.height},
        {foregroundSize.width, foregroundSize.height}, menu.parallax.offset);
    sprite(menuBackground_.Get(), rect(0, 0, backgroundSize.width, backgroundSize.height),
        backdrop.backgroundOrigin, backdrop.backgroundSize);
    sprite(menuForeground_.Get(), rect(0, 0, foregroundSize.width, foregroundSize.height),
        backdrop.foregroundOrigin, backdrop.foregroundSize);
    brush_->SetColor(D2D1::ColorF(0x080d14, menu.page == MenuPage::Main ? .12f : .78f));
    target_->FillRectangle(rect(0, 0, extent.x, extent.y), brush_.Get());
    const auto panel = [&](UiRect area, unsigned color, float opacity = 1.0f) {
        brush_->SetColor(D2D1::ColorF(color, opacity));
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect(area.x, area.y, area.width, area.height), 5, 5), brush_.Get());
    };
    const auto button = [&](UiRect area, const std::wstring& caption, bool primary = false) {
        panel(area, area.contains(mouse) ? 0x584637 : (primary ? 0x302c26 : 0x1b2023), .94f);
        brush_->SetColor(D2D1::ColorF(area.contains(mouse) ? 0xd6b57a : 0x8c7957));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(rect(area.x, area.y, area.width, area.height), 5, 5), brush_.Get(), 1);
        text(caption, rect(area.x + 18, area.y + 13, area.width - 30, area.height - 16), 0xe2e6d5, true);
    };
    if (menu.page == MenuPage::Main) {
        const auto logoSize = logo_->GetSize();
        sprite(logo_.Get(), rect(0, 0, logoSize.width, logoSize.height), {layout.logo.x, layout.logo.y}, {layout.logo.width, layout.logo.height});
        if (menu.canResume) button(layout.resume, L"Продолжить", true);
        button(layout.battles, L"Сражения", true);
        button(layout.library, L"Либрарий");
        button(layout.exit, L"Выйти");
    } else if (menu.page == MenuPage::Library) {
        drawLibrary(menu, definitions, mouse);
    } else {
        prepareLandscape(game.landscape(), game.map());
        text(L"VISAGES OF WAR", rect(34, 22, 380, 45), 0xe5d2a7, true);
        line({34, 80}, {extent.x - 34, 80}, 0x8c7957);
        text(L"Сражения", rect(layout.map.x, layout.map.y - 54, 500, 44), 0xe5e8d7, true);
        panel(layout.map, 0x15272c);
        const float zoom = std::min(376.0f / (game.map().width() * WorldView::tileSize),
                                    180.0f / (game.map().height() * WorldView::tileSize));
        const WorldView preview{{layout.map.x + (420 - game.map().width() * WorldView::tileSize * zoom) * .5f, layout.map.y + 22}, zoom};
        for (int y = 0; y < game.map().height(); ++y) for (int x = 0; x < game.map().width(); ++x)
            tile(game.map(), {x, y}, preview, false);
        for (const auto& crystal : game.crystals()) {
            const auto p = preview.project(center(crystal.cell), float(game.map().at(crystal.cell).height));
            drawCrystal(crystal, p, std::max(preview.zoom, .12f));
        }
        text(L"Тестовая долина", rect(layout.map.x + 22, layout.map.y + 217, 380, 34), 0xe5e8d7, true);
        text(L"1 игрок   •   64 × 64   •   Высоты −1, 0, 1, 2", rect(layout.map.x + 22, layout.map.y + 260, 380, 26), 0x91aba5);
        text(L"Выбрана", rect(layout.map.x + 22, layout.map.y + 286, 380, 25), 0xa0c789);
        const auto& commander = definitions.commanders().at(menu.commanderIndex);
        text(L"Игрок 1", rect(layout.commander.x, layout.map.y + 4, 390, 40), 0xe5e8d7, true);
        text(L"Командир", rect(layout.commander.x, layout.commander.y - 28, 380, 24), 0x91aba5);
        panel(layout.commander, 0x1d3339);
        text(wide(commander.displayName) + L"  ▾", rect(layout.commander.x + 15, layout.commander.y + 12, 370, 26), 0xe0e5d8);
        text(L"Раса: " + wide(commander.factionName), rect(layout.commander.x, layout.commander.y + 52, 380, 24), 0xa6b6b1);
        text(L"Цвет команды · " + std::to_wstring(teamPalettes.size()) + L" цветов", rect(layout.color.x, layout.color.y - 29, 380, 24), 0x91aba5);
        button(layout.back, L"Назад");
        button(layout.start, L"Начать сражение", true);
        drawColorSelect(ColorSelectLayout(layout.color, extent.y), menu, mouse);
        if (menu.commanderDropdown) {
            const size_t count = std::min(size_t{5}, definitions.commanders().size() - menu.commanderScroll);
            for (size_t i = 0; i < count; ++i) {
                UiRect option{layout.commanderOption.x, layout.commanderOption.y + i * 44.0f, layout.commanderOption.width, 44};
                panel(option, option.contains(mouse) ? 0x38534f : 0x233a3c);
                text(wide(definitions.commanders()[menu.commanderScroll + i].displayName), rect(option.x + 14, option.y + 10, option.width - 28, 28), 0xe0e5d8);
            }
        }
    }
    if (menu.page != MenuPage::Library)
        text(L"Visages of War  •  " + std::wstring(projectVersion), rect(34, extent.y - 34, extent.x - 68, 24), 0xd0c4aa);
    const auto hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) discardTarget();
    else check(hr);
}
}
