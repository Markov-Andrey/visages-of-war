#pragma once
#include "rts/Match.hpp"
#include "rts/Types.hpp"
#include <array>
#include <algorithm>

namespace rts {
enum class MenuPage { Main, BattleSetup, Playing };
struct MenuState {
    MenuPage page = MenuPage::Main;
    PlayerSettings player{};
    bool commanderDropdown{};
    size_t commanderIndex{};
    size_t commanderScroll{};
    bool mapSelected = true;
    bool canResume{};
};
struct UiRect {
    float x, y, width, height;
    bool contains(Vec2 point) const {
        return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height;
    }
};
struct MenuLayout {
    UiRect logo, battles, resume, exit, map, commander, commanderOption, back, start;
    std::array<UiRect, 6> colors;
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
        exit = {battles.x, battles.y + 70, buttonWidth, 50};
        map = {x, top, 420, 318};
        commander = {x + 470, top + 104, 400, 46};
        commanderOption = {commander.x, commander.y + 48, commander.width, 44};
        for (size_t i = 0; i < colors.size(); ++i) colors[i] = {x + 470 + i * 65.0f, top + 220, 48, 48};
        back = {x, top + 354, 180, 52};
        start = {x + 590, top + 354, 280, 52};
    }
};
}
