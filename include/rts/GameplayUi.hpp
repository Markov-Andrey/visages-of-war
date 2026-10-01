#pragma once
#include "rts/Menu.hpp"
#include "rts/Simulation.hpp"
#include "rts/Minimap.hpp"
#include "rts/CommandUi.hpp"

namespace rts {
inline unsigned selectionColor(PlayerId owner, PlayerId player) {
    if (owner == neutralPlayer) return 0xffd34d;
    return owner == player ? 0x75dc91 : 0xe86464;
}
inline UiRect buildingStageBounds(const BuildingSpriteStage& stage, float scale, Vec2 ground, float zoom) {
    const Vec2 extent = Vec2{float(stage.source[2]), float(stage.source[3])} * (scale * zoom);
    return {ground.x - extent.x * stage.anchor.x, ground.y - extent.y * stage.anchor.y, extent.x, extent.y};
}
UiRect unitBounds(const Simulation& game, const Unit& unit, const WorldView& view);
inline UiRect buildingLayerBounds(const BuildingSpriteStage& stage, const BuildingSpriteLayer& layer, UiRect bounds) {
    const float sx = bounds.width / stage.source[2], sy = bounds.height / stage.source[3];
    const auto& d = layer.destination;
    return {bounds.x + d[0] * sx, bounds.y + d[1] * sy, d[2] * sx, d[3] * sy};
}
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
    bool idleWorker(const Simulation& game, EntityId after = 0);
};
struct GameplayUi {
    Selection selection;
    std::optional<UiRect> drag;
    std::string placement;
    bool rallyMode{};
    bool buildMenu{};
    std::optional<OrderKind> orderMode;
    EntityId idleWorkerCursor{};
    Vec2 mouse{-1, -1};
};
inline bool commandEnabled(const Simulation& game, const GameplayUi& ui, UnitCommand command) {
    return std::any_of(ui.selection.ids.begin(), ui.selection.ids.end(), [&](EntityId id) {
        const auto* unit = game.unit(id);
        return unit && unit->owner == game.player().id && unit->health > 0 && commandCapable(*unit, command);
    });
}
inline bool rallyPointVisible(const Simulation& game, const Building& building, const GameplayUi& ui) {
    return building.owner == game.player().id && building.owner != neutralPlayer && building.health > 0 &&
        building.complete() && ui.selection.contains(building.id) && !building.definition.trainableUnits.empty() &&
        game.map().contains(building.rally);
}
inline bool rallyPointLightVisible(const Simulation& game, const Building& building, const GameplayUi& ui) {
    return rallyPointVisible(game, building, ui) && game.fog().visible(building.rally);
}
struct BattleLayout {
    UiRect world, minimap, info, menu, army, hero, idleWorker;
    std::array<UiRect, 9> commands{};
    size_t commandCount;
    explicit BattleLayout(Vec2 size, bool buildingCommands = false) : commandCount(buildingCommands ? 6 : 9) {
        world = {0, 58, size.x, std::max(1.0f, size.y - 262)};
        minimap = {18, size.y - 186, 164, 164};
        info = {204, size.y - 186, size.x - 490, 164};
        menu = {18, 12, 100, 34};
        army = {18, 72, 164, 40};
        hero = {18, 120, 164, 86};
        idleWorker = {18, size.y - 302, 56, 56};
        for (size_t i = 0; i < commandCount; ++i)
            commands[i] = {size.x - 200 + (i % 3) * 62.0f, size.y - 190 + (i / 3) * 62.0f, 56, 56};
    }
    std::optional<Cell> minimapCell(Vec2 p, const Map& map) const { return MinimapProjection(minimap, map).pick(p); }
};
}
