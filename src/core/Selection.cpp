#include "rts/GameplayUi.hpp"

namespace rts {
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
    const auto p = view.project(center(crystal.cell), float(game.map().at(crystal.cell).height));
    return {p.x - 32 * view.zoom, p.y - 74 * view.zoom, 64 * view.zoom, 80 * view.zoom};
}
std::optional<EntityId> pickEntity(const Simulation& game, const WorldView& view, Vec2 point) {
    std::optional<EntityId> result;
    float depth = -1;
    // Match drawing order: crystals first, then buildings and units at the same depth.
    for (size_t i = 0; i < game.crystals().size(); ++i) {
        const auto& node = game.crystals()[i];
        const float d = node.cell.y + .5f;
        if (node.remaining > 0 && game.fog().visible(node.cell) && crystalBounds(game, node, view).contains(point) && d >= depth) { result = node.id; depth = d; }
    }
    for (const auto& b : game.buildings()) {
        const float d = buildingDepth(b);
        if (b.owner == game.player().id && buildingBounds(game, b, view).contains(point) && d >= depth) { result = b.id; depth = d; }
    }
    for (const auto& u : game.units()) {
        const float d = unitDrawDepth(u.position) + (airborne(u.definition.movement) ? 1000.0f : 0);
        if (u.owner == game.player().id && game.fog().visible(u.cell) && unitBounds(game, u, view).contains(point) && d >= depth) { result = u.id; depth = d; }
    }
    return result;
}
void Selection::click(const Simulation& game, const WorldView& view, Vec2 point, bool additive) {
    activeType_.clear();
    const auto id = pickEntity(game, view, point);
    if (!additive) ids.clear();
    if (!id) return;
    const auto it = std::find(ids.begin(), ids.end(), *id);
    if (it != ids.end()) ids.erase(it);
    else {
        // Buildings and neutral resources are selected singly, outside mobile groups.
        if (!game.unit(*id) || (!ids.empty() && !game.unit(ids.front()))) ids.clear();
        ids.push_back(*id);
    }
}
void Selection::box(const Simulation& game, const WorldView& view, UiRect bounds, bool additive) {
    activeType_.clear();
    if (!additive || (!ids.empty() && !game.unit(ids.front()))) ids.clear();
    for (const auto& u : game.units()) {
        const auto p = unitScreenAnchor(view, u.position, game.unitHeight(u)) + Vec2{0, -20 * view.zoom};
        if (u.owner == game.player().id && game.fog().visible(u.cell) && bounds.contains(p) && !contains(u.id)) ids.push_back(u.id);
    }
}
void Selection::prune(const Simulation& game) {
    std::erase_if(ids, [&](EntityId id) {
        if (game.unit(id) || game.building(id)) return false;
        const auto* node = game.crystal(id);
        return !node || node->remaining <= 0 || !game.fog().visible(node->cell);
    });
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
