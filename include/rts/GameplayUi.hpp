#pragma once
#include "rts/UnitPresentation.hpp"
#include "rts/Menu.hpp"
#include "rts/Simulation.hpp"
#include "rts/Minimap.hpp"
#include "rts/CommandUi.hpp"
#include <chrono>
#include <string_view>

namespace rts {
inline constexpr unsigned friendlySelectionColor = 0x75dc91;
inline unsigned selectionColor(PlayerId owner, PlayerId player) {
    if (owner == neutralPlayer) return 0xffd34d;
    return owner == player ? friendlySelectionColor : 0xe86464;
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
inline float buildingDepth(const Building& building) { return building.origin.x + building.origin.y + building.definition.width + building.definition.height - .3f; }
bool buildingVisible(const Simulation& game, const Building& building);
bool selectableEntity(const Simulation& game, EntityId id);
std::optional<EntityId> pickEntity(const Simulation& game, const WorldView& view, Vec2 point);
struct SelectionGroup {
    std::string type;
    std::vector<EntityId> ids;
};
struct Selection {
    std::vector<EntityId> ids;
    bool contains(EntityId id) const { return std::find(ids.begin(), ids.end(), id) != ids.end(); }
    void click(const Simulation& game, const WorldView& view, Vec2 point, bool additive);
    bool selectTypeInView(const Simulation& game, const WorldView& view, UiRect viewport, Vec2 point, bool additive);
    void box(const Simulation& game, const WorldView& view, UiRect bounds, bool additive);
    void prune(const Simulation& game);
    void army(const Simulation& game);
    bool hero(const Simulation& game);
    bool idleWorker(const Simulation& game, EntityId after = 0);
    std::vector<SelectionGroup> groups(const Simulation& game) const;
    SelectionGroup activeGroup(const Simulation& game) const;
    const Unit* activeUnit(const Simulation& game) const;
    const Unit* inspectedUnit(const Simulation& game) const;
    bool cycleGroup(const Simulation& game, bool reverse = false);
    bool activateGroup(const Simulation& game, EntityId member);
    bool selectMember(const Simulation& game, EntityId member);
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
class GameplayNotification {
public:
    using Clock = std::chrono::steady_clock;
    std::wstring_view text(const Simulation& game, Clock::time_point now = Clock::now()) {
        if (!revision_ || *revision_ != game.messageRevision()) {
            revision_ = game.messageRevision();
            expires_ = now + std::chrono::seconds(4);
        }
        return now < expires_ ? std::wstring_view(game.message()) : std::wstring_view{};
    }
private:
    std::optional<std::uint64_t> revision_;
    Clock::time_point expires_{};
};
struct CommandFeedback {
    using Clock = std::chrono::steady_clock;
    static constexpr float pulseSeconds = .42f, pulseDelay = .20f;
    EntityId target{};
    mutable std::optional<Clock::time_point> began;
    void confirm(EntityId id) { target = id; began.reset(); }
    float age(Clock::time_point now = Clock::now()) const {
        if (!target) return -1;
        // Start with the first presented frame, including a paused match.
        if (!began) began = now;
        const float seconds = std::chrono::duration<float>(now - *began).count();
        return seconds >= 0 && seconds < pulseSeconds + pulseDelay ? seconds : -1;
    }
};
struct GameplayUi {
    // Presentation cache: notification lifetime continues while the simulation is paused.
    mutable GameplayNotification notification;
    CommandFeedback commandFeedback;
    Selection selection;
    ControlGroups controlGroups;
    std::optional<UiRect> drag;
    std::string placement;
    bool consoleOpen{};
    std::wstring consoleInput, consoleReply;
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
inline const AbilityDefinition* abilityCommandAt(const Simulation& game, const GameplayUi& ui, size_t slot) {
    if (ui.buildMenu || slot < firstAbilitySlot || slot >= firstAbilitySlot + abilityKeys.size()) return nullptr;
    const auto* unit = ui.selection.activeUnit(game);
    const auto index = slot - firstAbilitySlot;
    return unit && index < unit->definition.abilities.size() ? &unit->definition.abilities[index] : nullptr;
}
inline bool unitCommandVisible(const Simulation& game, const GameplayUi& ui, size_t slot) {
    if (!ui.selection.activeUnit(game)) return false;
    if (abilityCommandAt(game, ui, slot)) return true;
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
    UiRect world, minimap, commandPanel, info, menu, army, hero, idleWorker, notification;
    std::array<UiRect, commandSlots> commands{};
    std::array<UiRect, controlGroupCount> controlGroups{};
    static constexpr size_t commandCount = commandSlots;
    explicit BattleLayout(Vec2 size) {
        world = {0, 58, size.x, std::max(1.0f, size.y - 262)};
        constexpr float commandGap = 6, baseCommandSide = 64;
        constexpr float fullPanelSide = commandColumns * baseCommandSide + (commandColumns - 1) * commandGap;
        const float preferredPanelSide = size.x < 1060 ? std::clamp(size.x * .23f, 184.f, 224.f) : fullPanelSide;
        // Keep room above both blocks for notifications, groups and the hero shortcut in short windows.
        const float miniSide = std::min(preferredPanelSide, std::max(184.f, size.y - 360));
        minimap = {18, size.y - miniSide - 22, miniSide, miniSide};
        commandPanel = {size.x - minimap.x - miniSide, minimap.y, miniSide, miniSide};
        const float commandSide = (commandPanel.width - (commandColumns - 1) * commandGap) / commandColumns;
        const float commandPitch = commandSide + commandGap;
        const float infoLeft = minimap.x + minimap.width + 22;
        info = {infoLeft, size.y - 186, commandPanel.x - infoLeft - 16, 164};
        menu = {18, 12, 100, 34};
        army = {18, 72, 164, 40};
        hero = {18, 120, 164, 104};
        idleWorker = {18, minimap.y - 68, 56, 56};
        const float groupTop = std::min(size.y - 278, commandPanel.y - 40);
        for (size_t i = 0; i < controlGroupCount; ++i)
            controlGroups[i] = {infoLeft + i * 54.0f, groupTop, 48, 32};
        for (size_t i = 0; i < commandCount; ++i)
            commands[i] = {commandPanel.x + (i % commandColumns) * commandPitch,
                commandPanel.y + (i / commandColumns) * commandPitch, commandSide, commandSide};
        const float messageWidth = std::min(760.f, size.x - 64), messageHeight = 48;
        notification = {(size.x - messageWidth) * .5f, controlGroups.front().y - messageHeight - 12, messageWidth, messageHeight};
    }
    std::optional<Cell> minimapCell(Vec2 p, const Map& map) const { return MinimapProjection(minimap, map).pick(p); }
};
inline bool mouseInBattleWorld(const GameplayUi& ui, const BattleLayout& layout, Vec2 mouse) {
    for (size_t i = 0; i < controlGroupCount; ++i)
        if (!ui.controlGroups.members(i).empty() && layout.controlGroups[i].contains(mouse)) return false;
    return layout.world.contains(mouse) && !layout.army.contains(mouse) && !layout.hero.contains(mouse) &&
        !layout.idleWorker.contains(mouse) && !layout.minimap.contains(mouse) && !layout.commandPanel.contains(mouse);
}
inline EntityId hoveredEntity(const Simulation& game, const WorldView& view, const GameplayUi& ui, const BattleLayout& layout) {
    if (ui.consoleOpen || ui.drag || !ui.placement.empty() || ui.rallyMode || ui.orderMode || !mouseInBattleWorld(ui, layout, ui.mouse)) return 0;
    const auto id = pickEntity(game, view, ui.mouse);
    return id && (game.unit(*id) || game.building(*id)) ? *id : 0;
}
struct SelectionPanelLayout {
    UiRect portrait, health, mana, content;
    explicit SelectionPanelLayout(UiRect info) {
        const float side = std::clamp(info.width * .25f, 96.0f, 120.0f);
        portrait = {info.x, info.y + (info.height - side - 44) * .5f, side, side};
        health = {portrait.x, portrait.y + side + 4, side, 20};
        mana = {portrait.x, health.y + 20, side, 20};
        content = {info.x + side + 16, info.y, info.width - side - 16, info.height};
    }
};
struct ProductionQueueLayout {
    std::array<UiRect, Building::productionQueueLimit> slots;
    std::optional<size_t> pick(Vec2 point, size_t count) const {
        for (size_t i = 0; i < std::min(count, slots.size()); ++i)
            if (slots[i].contains(point)) return i;
        return std::nullopt;
    }
    explicit ProductionQueueLayout(UiRect content) {
        constexpr float gap = 4;
        const size_t columns = content.width >= 10 * 36 + 9 * gap ? 10 : 5;
        const size_t rows = (slots.size() + columns - 1) / columns;
        const float side = std::max(1.f, std::min({48.f, (content.width - (columns - 1) * gap) / columns,
            (content.height - 96 - (rows - 1) * gap) / rows}));
        for (size_t i = 0; i < slots.size(); ++i)
            slots[i] = {content.x + (i % columns) * (side + gap), content.y + 96 + (i / columns) * (side + gap), side, side};
    }
};
struct SelectionCard {
    EntityId id;
    UiRect bounds;
    bool active;
    UiRect healthBar() const { return {bounds.x, bounds.y + bounds.height + 3, bounds.width, 4}; }
    UiRect manaBar() const { return {bounds.x, bounds.y + bounds.height + 9, bounds.width, 3}; }
};
struct SelectionCards {
    std::vector<SelectionCard> cards;
    size_t first{}, total{};
    SelectionCards(const Simulation& game, const Selection& selection, UiRect info);
};
}
