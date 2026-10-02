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
struct SelectionGroup {
    std::string type;
    std::vector<EntityId> ids;
};
struct Selection {
    std::vector<EntityId> ids;
    bool contains(EntityId id) const { return std::find(ids.begin(), ids.end(), id) != ids.end(); }
    void click(const Simulation& game, const WorldView& view, Vec2 point, bool additive);
    void box(const Simulation& game, const WorldView& view, UiRect bounds, bool additive);
    void prune(const Simulation& game);
    void army(const Simulation& game);
    bool hero(const Simulation& game);
    bool idleWorker(const Simulation& game, EntityId after = 0);
    std::vector<SelectionGroup> groups(const Simulation& game) const;
    SelectionGroup activeGroup(const Simulation& game) const;
    const Unit* activeUnit(const Simulation& game) const;
    bool cycleGroup(const Simulation& game, bool reverse = false);
    bool activateGroup(const Simulation& game, EntityId member);
private:
    std::string activeType_;
};
inline constexpr size_t controlGroupCount = 10;
inline constexpr wchar_t controlGroupKey(size_t slot) { return slot == 9 ? L'0' : wchar_t(L'1' + slot); }
class ControlGroups {
public:
    void bind(size_t slot, const Simulation& game, const Selection& selection);
    bool recall(size_t slot, const Simulation& game, Selection& selection);
    void prune(const Simulation& game);
    const std::vector<EntityId>& members(size_t slot) const { return groups_.at(slot); }
    bool selected(size_t slot, const Selection& selection) const;
private:
    std::array<std::vector<EntityId>, controlGroupCount> groups_;
};
struct GameplayUi {
    Selection selection;
    ControlGroups controlGroups;
    std::optional<UiRect> drag;
    std::string placement;
    bool rallyMode{};
    bool buildMenu{};
    std::optional<OrderKind> orderMode;
    std::string commandGroup;
    EntityId idleWorkerCursor{};
    Vec2 mouse{-1, -1};
};
inline bool commandEnabled(const Simulation& game, const GameplayUi& ui, UnitCommand command) {
    const auto group = ui.selection.activeGroup(game);
    return std::any_of(group.ids.begin(), group.ids.end(), [&](EntityId id) {
        const auto* unit = game.unit(id);
        return unit && unit->owner == game.player().id && unit->health > 0 && commandCapable(*unit, command);
    });
}
inline bool unitCommandVisible(const Simulation& game, const GameplayUi& ui, size_t slot) {
    if (!ui.selection.activeUnit(game)) return false;
    if (slot == backCommandSlot) return ui.buildMenu || ui.orderMode || !ui.placement.empty();
    if (ui.buildMenu) return slot < 3 && slot < game.buildingTypes().size() && commandEnabled(game, ui, UnitCommand::Build);
    const auto* command = unitCommandAt(slot);
    return command && commandEnabled(game, ui, command->command);
}
inline std::vector<EntityId> commandRecipients(const Simulation& game, const GameplayUi& ui, OrderKind order) {
    for (const auto& command : unitCommands) if (command.order == order && command.scope == CommandScope::ActiveGroup) {
        const auto group = ui.selection.activeGroup(game);
        return ui.commandGroup.empty() || ui.commandGroup == group.type ? group.ids : std::vector<EntityId>{};
    }
    return ui.selection.ids;
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
    std::array<UiRect, commandSlots> commands{};
    std::array<UiRect, controlGroupCount> controlGroups{};
    static constexpr size_t commandCount = commandSlots;
    explicit BattleLayout(Vec2 size) {
        world = {0, 58, size.x, std::max(1.0f, size.y - 262)};
        minimap = {18, size.y - 186, 164, 164};
        info = {204, size.y - 186, size.x - 482, 164};
        menu = {18, 12, 100, 34};
        army = {18, 72, 164, 40};
        hero = {18, 120, 164, 86};
        idleWorker = {18, size.y - 302, 56, 56};
        for (size_t i = 0; i < controlGroupCount; ++i)
            controlGroups[i] = {204 + i * 54.0f, size.y - 278, 48, 32};
        for (size_t i = 0; i < commandCount; ++i)
            commands[i] = {size.x - 262 + (i % commandColumns) * 62.0f, size.y - 190 + (i / commandColumns) * 62.0f, 56, 56};
    }
    std::optional<Cell> minimapCell(Vec2 p, const Map& map) const { return MinimapProjection(minimap, map).pick(p); }
};
struct SelectionPanelLayout {
    UiRect portrait, content;
    explicit SelectionPanelLayout(UiRect info) {
        const float side = std::clamp(info.width * .25f, 96.0f, 144.0f);
        portrait = {info.x, info.y + (info.height - side) * .5f, side, side};
        content = {info.x + side + 16, info.y, info.width - side - 16, info.height};
    }
};
struct SelectionCard {
    EntityId id;
    UiRect bounds;
    bool active;
    UiRect healthBar() const { return {bounds.x, bounds.y + bounds.height + 3, bounds.width, 4}; }
};
struct SelectionCards {
    std::vector<SelectionCard> cards;
    size_t first{}, total{};
    SelectionCards(const Simulation& game, const Selection& selection, UiRect info);
};
}
