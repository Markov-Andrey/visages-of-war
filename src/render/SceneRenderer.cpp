#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::draw(const Simulation& game, const WorldView& view, std::optional<Cell> hover,
                    const GameplayUi& ui, bool grid, bool paused) {
    ensureTarget();
    updateFogMask(game);
    prepareLandscape(game.landscape(), game.map());
    waterSeconds_ = float(game.clock().elapsedTicks() % 36000) / Simulation::ticksPerSecond;
    teamColor_ = teamRgb(game.player().color);
    enemyColor_ = teamRgb(game.player().color == TeamColor::Red ? TeamColor::Blue : TeamColor::Red);
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0x081119));
    const auto extent = size();
    const BattleLayout layout(extent);
    const auto hoverUnit = hoveredUnit(game, view, ui, layout);
    const float commandAge = ui.commandFeedback.age();
    const auto feedbackAge = [&](EntityId id) { return id == ui.commandFeedback.target ? commandAge : -1.f; };
    prepareNightLighting(game, view, extent, ui);
    const auto& map = game.map();
    const auto objectBounds = [&](const WorldObjectDefinition& definition, Vec2 position, float scale, float rotation) {
        const Cell cell{int(position.x), int(position.y)};
        const auto p = view.project(position, map.surfaceHeight(cell, position));
        const auto e = definition.size * (view.zoom * scale);
        if (rotation == 0) return UiRect{p.x - e.x * definition.anchor.x, p.y - e.y * definition.anchor.y, e.x, e.y};
        // Conservative rotation-independent bound, used only to reject non-overlapping masks.
        const float radius = std::hypot(e.x * std::max(definition.anchor.x, 1 - definition.anchor.x),
            e.y * std::max(definition.anchor.y, 1 - definition.anchor.y));
        return UiRect{p.x - radius, p.y - radius, radius * 2, radius * 2};
    };
    target_->PushAxisAlignedClip(rect(layout.world.x, layout.world.y, layout.world.width, layout.world.height), D2D1_ANTIALIAS_MODE_ALIASED);

    const auto onScreen = [&](Vec2 p) { return p.x > -250 && p.x < extent.x + 250 && p.y > -80 && p.y < extent.y; };
    enum class Kind { Crystal, Environment, Decoration, Building, Corpse, Bones, Unit, RallyPoint };
    struct Item { float depth; Kind kind; size_t index; };
    std::vector<Item> items;
    std::vector<const Building*> groundSelections;
    for (const auto& b : game.buildings()) if (b.health > 0 && buildingVisible(game, b) && (ui.selection.contains(b.id) || !b.complete()) &&
        onScreen(view.project(center(b.origin), float(map.at(b.origin).height)))) groundSelections.push_back(&b);
    for (size_t i = 0; i < game.landscape().decorations.size(); ++i) {
        const auto p = game.landscape().decorations[i].position;
        if (game.fog().explored({int(p.x), int(p.y)})) items.push_back({p.x + p.y, Kind::Decoration, i});
    }
    for (size_t i = 0; i < game.crystals().size(); ++i) if (game.knownCrystal(i) > 0)
        items.push_back({game.crystals()[i].depth(), Kind::Crystal, i});
    for (size_t i = 0; i < game.environment().size(); ++i) if (game.knownEnvironment(i)) {
        const auto& object = game.environment()[i];
        items.push_back({object.origin.x + object.origin.y + object.width + object.height - 1.0f, Kind::Environment, i});
    }
    for (size_t i = 0; i < game.buildings().size(); ++i) if (game.buildings()[i].health > 0 && buildingVisible(game, game.buildings()[i]))
        items.push_back({buildingDepth(game.buildings()[i]), Kind::Building, i});
    for (size_t i = 0; i < game.buildings().size(); ++i) if (rallyPointVisible(game, game.buildings()[i], ui))
        items.push_back({game.buildings()[i].rally.x + game.buildings()[i].rally.y + 1.0f, Kind::RallyPoint, i});
    for (size_t i = 0; i < game.corpses().size(); ++i) if (game.fog().visible(game.corpses()[i].cell))
        items.push_back({unitDrawDepth(game.corpses()[i].position), Kind::Corpse, i});
    for (size_t i = 0; i < game.bones().size(); ++i) if (game.fog().visible(game.bones()[i].cell))
        items.push_back({unitDrawDepth(game.bones()[i].position), Kind::Bones, i});
    for (size_t i = 0; i < game.units().size(); ++i) if (game.fog().visible(game.units()[i].cell) && !airborne(game.units()[i].definition.movement))
        items.push_back({unitDrawDepth(game.units()[i].position), Kind::Unit, i});
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });
    size_t nextItem = 0;
    // Draw back-to-front by ground diagonal, with all objects ordered by their ground anchor.
    // Foreground elevated terrain is allowed to occlude lower objects behind its edge.
    for (int y = 0; y < map.width() + map.height(); ++y) {
        terrainRow(map, y, view, grid, true);
        worldOpacity_ = 1;
        for (const auto* building : groundSelections) buildingGroundSelection(game, *building, view, y);
        while (nextItem < items.size() && items[nextItem].depth < y + 1) {
            const auto item = items[nextItem++];
            worldOpacity_ = 1;
            switch (item.kind) {
            case Kind::RallyPoint:
                drawRallyPoint(game, game.buildings()[item.index], view);
                break;
            case Kind::Decoration: {
                const auto& object = game.landscape().decorations[item.index];
                worldOpacity_ = game.fog().visible({int(object.position.x), int(object.position.y)}) ? 1.0f : .3f;
                const bool masked = worldOpacity_ == 1 && beginUnitOcclusion(game, view, item.depth,
                    objectBounds(worldAssets_.object(object.definitionId), object.position, object.scale, object.rotation));
                decoration(object, map, view);
                if (masked) target_->PopLayer();
                break;
            }
            case Kind::Crystal: {
                const auto& crystal = game.crystals()[item.index];
                const auto p = view.project(crystal.center(), float(map.at(crystal.cell).height));
                if (!onScreen(p)) break;
                worldOpacity_ = game.crystalVisible(crystal) ? 1.0f : .3f;
                if (ui.selection.contains(crystal.id) && game.crystalVisible(crystal)) {
                    drawSelectionRing(p, 28 * crystal.width, 13 * crystal.height, view.zoom,
                        selectionColor(crystal.owner, game.player().id));
                }
                if (game.crystalVisible(crystal)) drawCommandPulse(p, 28 * crystal.width, 13 * crystal.height, view.zoom,
                    selectionColor(crystal.owner, game.player().id), feedbackAge(crystal.id));
                drawCrystal(crystal, p, view.zoom, game.crystalVisible(crystal), game.clock().elapsedTicks());
                break;
            }
            case Kind::Environment: {
                auto object = game.environment()[item.index];
                if (!onScreen(view.project(center(object.origin), float(map.at(object.origin).height)))) break;
                object.hitPoints = std::max(1, object.hitPoints); // Last known image in explored fog.
                worldOpacity_ = game.environmentVisible(item.index) ? 1.0f : .45f;
                const auto p = view.project(center(object.origin), float(map.at(object.origin).height));
                UiRect bounds{p.x - 60 * view.zoom, p.y - 110 * view.zoom, (120 + object.width * 64.f) * view.zoom, 155 * view.zoom};
                if (!object.definitionId.empty() && !worldAssets_.object(object.definitionId).image.empty())
                    bounds = objectBounds(worldAssets_.object(object.definitionId),
                        {object.origin.x + object.width - .5f, object.origin.y + object.height - .5f}, 1, 0);
                const bool masked = worldOpacity_ == 1 && beginUnitOcclusion(game, view, item.depth, bounds);
                environmentObject(object, map, view);
                if (masked) target_->PopLayer();
                break;
            }
            case Kind::Building: {
                const auto& b = game.buildings()[item.index];
                if (onScreen(view.project(center(b.origin), float(map.at(b.origin).height)))) {
                    const bool masked = beginUnitOcclusion(game, view, item.depth, buildingBounds(game, b, view));
                    buildingSprite(game, b, view, ui.selection.contains(b.id));
                    if (masked) target_->PopLayer();
                }
                break;
            }
            case Kind::Corpse: {
                const auto& corpse = game.corpses()[item.index];
                const auto p = unitScreenAnchor(view, corpse.position, corpse.height);
                if (!onScreen(p)) break;
                corpseSprite(corpse, view, corpse.owner == game.player().id ? teamColor_ : enemyColor_);
                break;
            }
            case Kind::Bones: {
                const auto& bones = game.bones()[item.index];
                if (onScreen(unitScreenAnchor(view, bones.position, bones.height))) bonesSprite(bones, view);
                break;
            }
            case Kind::Unit: {
                const auto& u = game.units()[item.index];
                if (onScreen(unitScreenAnchor(view, u.position, game.unitHeight(u))))
                    unitSprite(game, u, view, ui.selection.contains(u.id), hoverUnit == u.id, feedbackAge(u.id));
                break;
            }
            }
        }
    }
    worldOpacity_ = 1;
    std::vector<const Unit*> flyers;
    for (const auto& u : game.units()) if (airborne(u.definition.movement) && game.fog().visible(u.cell)) flyers.push_back(&u);
    std::stable_sort(flyers.begin(), flyers.end(), [](const Unit* a, const Unit* b) { return unitDrawDepth(a->position) < unitDrawDepth(b->position); });
    for (const auto* u : flyers) unitSprite(game, *u, view, ui.selection.contains(u->id), hoverUnit == u->id, feedbackAge(u->id));
    drawProjectiles(game, view);
    if (grid) for (size_t i = 0; i < game.environment().size(); ++i) {
        const auto& object = game.environment()[i];
        if (!game.knownEnvironment(i)) continue;
        for (int y = 0; y < object.height; ++y) for (int x = 0; x < object.width; ++x) {
            const Cell c = object.origin + Cell{x, y};
            if (!game.fog().explored(c)) continue;
            const auto p = view.project(center(c), float(map.at(c).height));
            if (!onScreen(p)) continue;
            gridFootprint(map, c, view, object.blocks(x, y) ? 0xe16d65 : 0x73ca91);
        }
    }
    if (grid) for (const auto& u : game.units()) if (u.owner == game.player().id && ui.selection.contains(u.id)) {
        Vec2 previous = unitScreenAnchor(view, u.position, game.unitHeight(u));
        for (size_t i = u.next; i < u.route.size(); ++i) {
            const Cell c = u.route[i];
            if (!game.fog().explored(c)) break;
            const auto next = unitScreenAnchor(view, center(c), airborne(u.definition.movement) ? 5.0f : map.surfaceHeight(c, center(c)));
            line(previous, next, 0xe5ce92, 1.5f); previous = next;
        }
    }
    for (const auto& b : game.buildings()) if (rallyPointVisible(game, b, ui)) {
        const auto p = view.project(center(b.rally), map.surfaceHeight(b.rally, center(b.rally)));
        const auto start = view.project({b.origin.x + b.definition.width * .5f, b.origin.y + b.definition.height * .5f}, float(map.at(b.origin).height));
        for (int i = 0; i < 20; i += 2) line(start + (p - start) * (i / 20.0f), start + (p - start) * ((i + 1) / 20.0f), 0xb6d7ba, 1.5f);
    }
    nightActive_ = false;
    if (!ui.placement.empty() && hover) {
        const auto& type = game.entityType(ui.placement);
        const unsigned colorPreview = game.canPlace(ui.placement, *hover) ? 0x7cdeb0 : 0xeb7c77;
        for (int y = 0; y < type.height; ++y) for (int x = 0; x < type.width; ++x) {
            const Cell c = *hover + Cell{x, y};
            if (!map.contains(c)) continue;
            const auto footprint = map.surfaceCorners(c, view);
            polygon(footprint, colorPreview, .35f); polygon(footprint, colorPreview, 1, false);
        }
    }
    if (ui.drag) {
        const auto d = *ui.drag;
        brush_->SetColor(D2D1::ColorF(0x8ed9bb, .12f)); target_->FillRectangle(rect(d.x, d.y, d.width, d.height), brush_.Get());
        brush_->SetColor(D2D1::ColorF(0x8ed9bb)); target_->DrawRectangle(rect(d.x, d.y, d.width, d.height), brush_.Get(), 1);
    }
    target_->PopAxisAlignedClip();
    hud(game, ui, paused, view, grid);
    const auto hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) discardTarget(); else check(hr);
}
}
