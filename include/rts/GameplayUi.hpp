#pragma once
#include "rts/Menu.hpp"
#include "rts/Simulation.hpp"
#include "rts/Minimap.hpp"

namespace rts {
UiRect unitBounds(const Simulation& game, const Unit& unit, const WorldView& view);
UiRect buildingBounds(const Simulation& game, const Building& building, const WorldView& view);
UiRect crystalBounds(const Simulation& game, const Crystal& crystal, const WorldView& view);
inline float buildingDepth(const Building& building) { return building.origin.y + building.definition.height - .15f; }
std::optional<EntityId> pickEntity(const Simulation& game, const WorldView& view, Vec2 point);
struct Selection {
    std::vector<EntityId> ids;
    bool contains(EntityId id) const { return std::find(ids.begin(), ids.end(), id) != ids.end(); }
    void click(const Simulation& game, const WorldView& view, Vec2 point, bool additive);
    void box(const Simulation& game, const WorldView& view, UiRect bounds, bool additive);
    void prune(const Simulation& game);
    void army(const Simulation& game);
    bool hero(const Simulation& game);
};
struct GameplayUi {
    Selection selection;
    std::optional<UiRect> drag;
    std::string placement;
    bool rallyMode{};
    Vec2 mouse{-1, -1};
};
struct BattleLayout {
    UiRect world, minimap, info, menu, army, hero;
    std::array<UiRect, 6> commands;
    explicit BattleLayout(Vec2 size) {
        world = {0, 58, size.x, std::max(1.0f, size.y - 262)};
        minimap = {18, size.y - 186, 164, 164};
        info = {204, size.y - 186, size.x - 594, 164};
        menu = {18, 12, 100, 34};
        army = {18, 72, 164, 40};
        hero = {18, 120, 164, 86};
        for (size_t i = 0; i < commands.size(); ++i)
            commands[i] = {size.x - 304 + (i % 3) * 98.0f, size.y - 190 + (i / 3) * 94.0f, 88, 88};
    }
    std::optional<Cell> minimapCell(Vec2 p, const Map& map) const { return MinimapProjection(minimap, map).pick(p); }
};
}
