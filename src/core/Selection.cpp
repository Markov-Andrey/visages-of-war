#include "rts/GameplayUi.hpp"
#include "rts/UnitOcclusion.hpp"

namespace rts {
namespace {
bool onlyPlayerUnits(const Simulation& game, std::span<const EntityId> ids) {
    return std::all_of(ids.begin(), ids.end(), [&](EntityId id) {
        const auto* unit = game.unit(id);
        return unit && unit->owner == game.player().id;
    });
}
}
bool buildingVisible(const Simulation& game, const Building& building) {
    for (int y = 0; y < building.definition.height; ++y) for (int x = 0; x < building.definition.width; ++x)
        if (game.fog().visible(building.origin + Cell{x, y})) return true;
    return false;
}
bool selectableEntity(const Simulation& game, EntityId id) {
    if (const auto* unit = game.unit(id)) return unit->health > 0 && game.fog().visible(unit->cell);
    if (const auto* building = game.building(id)) return building->health > 0 && buildingVisible(game, *building);
    const auto* crystal = game.crystal(id);
    return crystal && crystal->remaining > 0 && game.crystalVisible(*crystal);
}
void Selection::army(const Simulation& game) {
    activeType_.clear();
    ids.clear();
    // Support units belong to the army even when they do not have a weapon.
    for (const auto& u : game.units()) if (u.owner == game.player().id && u.health > 0 && !u.definition.isWorker()) ids.push_back(u.id);
}
bool Selection::hero(const Simulation& game) {
    if (const auto* u = game.hero()) { ids = {u->id}; activeType_.clear(); return true; }
    return false;
}
UiRect unitBounds(const Simulation& game, const Unit& unit, const WorldView& view) {
    const auto p = unitScreenAnchor(view, unit.position, game.unitHeight(unit));
    const auto& s = unit.definition.sprite;
    const auto extent = s.size * view.zoom;
    return {p.x + (.225f - s.anchor.x) * extent.x, p.y + (.0625f - s.anchor.y) * extent.y, .55f * extent.x, .825f * extent.y};
}
UiRect buildingBounds(const Simulation& game, const Building& building, const WorldView& view) {
    const auto& type = building.definition;
    const auto p = view.project({building.origin.x + type.width * 0.5f, building.origin.y + type.height * 0.5f}, float(game.map().at(building.origin).height));
    if (const auto* stage = type.buildingSprite.stage(building.constructionProgress, type.constructionTicks))
        return buildingStageBounds(*stage, type.buildingSprite.scale, p, view.zoom);
    const float w = type.visual == EntityVisual::Tower ? 90.0f : type.visual == EntityVisual::Barracks ? 250.0f : 210.0f;
    const float h = type.visual == EntityVisual::Tower ? 165.0f : 232.0f;
    return {p.x - w * .5f * view.zoom, p.y - (h - 55) * view.zoom, w * view.zoom, h * view.zoom};
}
UiRect crystalBounds(const Simulation& game, const Crystal& crystal, const WorldView& view) {
    const auto p = view.project(crystal.center(), float(game.map().at(crystal.cell).height));
    const float width = 64.0f * crystal.width, top = 74.0f + 24 * (crystal.height - 1);
    return {p.x - width * .5f * view.zoom, p.y - top * view.zoom, width * view.zoom,
        (80 + 48.0f * (crystal.height - 1)) * view.zoom};
}
std::optional<EntityId> pickEntity(const Simulation& game, const WorldView& view, Vec2 point) {
    std::optional<EntityId> result;
    float depth = -1;
    // Match drawing order: crystals first, then buildings and units at the same depth.
    for (size_t i = 0; i < game.crystals().size(); ++i) {
        const auto& node = game.crystals()[i];
        const float d = node.depth();
        if (node.remaining > 0 && game.crystalVisible(node) && crystalBounds(game, node, view).contains(point) && d >= depth) { result = node.id; depth = d; }
    }
    for (const auto& b : game.buildings()) {
        const float d = buildingDepth(b);
        if (b.health > 0 && buildingVisible(game, b) && buildingBounds(game, b, view).contains(point) && d >= depth) { result = b.id; depth = d; }
    }
    const auto* coveringBuilding = result ? game.building(*result) : nullptr;
    if (coveringBuilding) {
        // Remove only the covering building from depth competition inside a window.
        for (const auto& u : game.units()) if (occlusionEligible(game, u, buildingDepth(*coveringBuilding)) &&
            unitOcclusion(game, u, view).contains(point)) { result.reset(); depth = -1; break; }
    }
    for (const auto& u : game.units()) {
        const float d = unitDrawDepth(u.position) + (airborne(u.definition.movement) ? 1000.0f : 0);
        const bool throughWindow = coveringBuilding && occlusionEligible(game, u, buildingDepth(*coveringBuilding)) &&
            unitOcclusion(game, u, view).contains(point);
        if (u.health > 0 && game.fog().visible(u.cell) && (unitBounds(game, u, view).contains(point) || throughWindow) && d >= depth) { result = u.id; depth = d; }
    }
    return result;
}
void Selection::click(const Simulation& game, const WorldView& view, Vec2 point, bool additive) {
    activeType_.clear();
    const auto id = pickEntity(game, view, point);
    if (!additive) ids.clear();
    if (!id) return;
    const auto* unit = game.unit(*id);
    // Foreign units, buildings and resources are inspected singly, never mixed with an army.
    if (!unit) {
        if (additive && ids.size() == 1 && ids.front() == *id) ids.clear();
        else ids = {*id};
        return;
    }
    if (unit->owner != game.player().id) { ids = {*id}; return; }
    if (!onlyPlayerUnits(game, ids)) ids.clear();
    const auto it = std::find(ids.begin(), ids.end(), *id);
    if (it != ids.end()) ids.erase(it);
    else ids.push_back(*id);
}
bool Selection::selectTypeInView(const Simulation& game, const WorldView& view, UiRect viewport, Vec2 point, bool additive) {
    if (!viewport.contains(point)) return false;
    const auto id = pickEntity(game, view, point);
    const auto* clicked = id ? game.unit(*id) : nullptr;
    if (!clicked || clicked->owner != game.player().id || clicked->health <= 0) return false;
    if (!additive || !onlyPlayerUnits(game, ids)) ids.clear();
    activeType_ = clicked->definition.id;
    for (const auto& unit : game.units()) {
        if (unit.owner != game.player().id || unit.health <= 0 || unit.definition.id != clicked->definition.id ||
            !game.fog().visible(unit.cell) || contains(unit.id)) continue;
        const auto bounds = unitBounds(game, unit, view);
        if (bounds.x < viewport.x + viewport.width && bounds.x + bounds.width > viewport.x &&
            bounds.y < viewport.y + viewport.height && bounds.y + bounds.height > viewport.y) ids.push_back(unit.id);
    }
    return true;
}
void Selection::box(const Simulation& game, const WorldView& view, UiRect bounds, bool additive) {
    activeType_.clear();
    if (!additive || !onlyPlayerUnits(game, ids)) ids.clear();
    for (const auto& u : game.units()) {
        const auto p = unitScreenAnchor(view, u.position, game.unitHeight(u)) + Vec2{0, -20 * view.zoom};
        if (u.owner == game.player().id && u.health > 0 && game.fog().visible(u.cell) && bounds.contains(p) && !contains(u.id)) ids.push_back(u.id);
    }
}
void Selection::prune(const Simulation& game) {
    std::erase_if(ids, [&](EntityId id) { return !selectableEntity(game, id); });
    if (!activeType_.empty()) activeType_ = activeGroup(game).type;
}
bool Selection::idleWorker(const Simulation& game, EntityId after) {
    EntityId first{}, next{};
    for (const auto& unit : game.units()) if (rts::idleWorker(game, unit)) {
        if (!first || unit.id < first) first = unit.id;
        if (unit.id > after && (!next || unit.id < next)) next = unit.id;
    }
    if (!next) next = first;
    if (!next) return false;
    ids = {next};
    activeType_.clear();
    return true;
}
}
