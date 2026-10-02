#pragma once
#include "rts/Match.hpp"
#include "rts/MenuBackdrop.hpp"
#include "rts/Types.hpp"
#include <array>
#include <algorithm>

namespace rts {
enum class MenuPage { Main, BattleSetup, Library, Playing };
struct MenuState {
    MenuPage page = MenuPage::Main;
    PlayerSettings player{};
    bool commanderDropdown{};
    bool colorDropdown{};
    size_t colorFocus{};
    size_t commanderIndex{};
    size_t commanderScroll{};
    bool mapSelected = true;
    bool canResume{};
    size_t libraryFaction{}, libraryEntry{}, libraryFactionScroll{}, libraryEntryScroll{};
    double librarySeconds{}; // Presentation clock; browsing never advances the match.
    MenuParallax parallax;
};
struct UiRect {
    float x, y, width, height;
    bool contains(Vec2 point) const {
        return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height;
    }
};
struct ColorSelectLayout {
    static constexpr size_t columns = 6;
    static constexpr size_t rows = (teamPalettes.size() + columns - 1) / columns;
    UiRect field, popup;
    ColorSelectLayout(UiRect area, float screenHeight) : field(area) {
        const float height = 12 + rows * 40.0f + 34;
        const float below = area.y + area.height + 6;
        popup = {area.x, below + height <= screenHeight - 12 ? below : std::max(12.0f, area.y - height - 6), area.width, height};
    }
    UiRect option(size_t index) const {
        const float width = (popup.width - 24 - (columns - 1) * 8) / columns;
        return {popup.x + 12 + (index % columns) * (width + 8), popup.y + 12 + (index / columns) * 40.0f, width, 32};
    }
    std::optional<size_t> pick(Vec2 point) const {
        for (size_t i = 0; i < teamPalettes.size(); ++i) if (option(i).contains(point)) return i;
        return std::nullopt;
    }
};
struct MenuLayout {
    UiRect logo, battles, library, resume, exit, map, commander, commanderOption, color, back, start;
    explicit MenuLayout(Vec2 size, bool canResume = false) {
        const float x = (size.x - 900) * 0.5f;
        const float top = std::max(125.0f, (size.y - 520) * 0.5f);
        const float columnWidth = size.x / 3;
        const float centre = columnWidth * .5f;
        const float logoWidth = std::min({540.0f, columnWidth - 32, size.y * .58f});
        const float buttonWidth = std::min(340.0f, columnWidth - 48);
        logo = {centre - logoWidth * .5f, size.y * .11f, logoWidth, logoWidth * (2.0f / 3)};
        const float menuTop = logo.y + logo.height + 16;
        resume = {centre - buttonWidth * .5f, menuTop, buttonWidth, 58};
        battles = {resume.x, menuTop + (canResume ? 70.0f : 0.0f), buttonWidth, 58};
        library = {battles.x, battles.y + 70, buttonWidth, 58};
        exit = {battles.x, library.y + 70, buttonWidth, 50};
        map = {x, top, 420, 318};
        commander = {x + 470, top + 104, 400, 46};
        commanderOption = {commander.x, commander.y + 48, commander.width, 44};
        color = {x + 470, top + 220, 400, 44};
        back = {x, top + 354, 180, 52};
        start = {x + 590, top + 354, 280, 52};
    }
};
}
