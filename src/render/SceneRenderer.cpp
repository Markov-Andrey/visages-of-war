#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::draw(const Simulation& game, const WorldView& view, std::optional<Cell> hover,
                    const GameplayUi& ui, bool grid, bool paused) {
    ensureTarget();
    updateFogMask(game);
    prepareLandscape(game.landscape(), game.map());
    const auto color = teamRgb(game.player().color);
    if (teamColor_ != color) { teamColor_ = color; loadBitmap(paths_.asset(L"sprites/worker.png"), worker_, teamColor_, SpriteTeamMask::Blue); }
    const auto enemyColor = teamRgb(game.player().color == TeamColor::Red ? TeamColor::Blue : TeamColor::Red);
    if (enemyColor_ != enemyColor) { enemyColor_ = enemyColor; loadBitmap(paths_.asset(L"sprites/worker.png"), enemy_, enemyColor_, SpriteTeamMask::Blue); }
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0x081119));
    const auto extent = size();
    const BattleLayout layout(extent);
    prepareNightLighting(game, view, extent, ui);
    const auto& map = game.map();
    target_->PushAxisAlignedClip(rect(layout.world.x, layout.world.y, layout.world.width, layout.world.height), D2D1_ANTIALIAS_MODE_ALIASED);
    const auto onScreen = [&](Vec2 p) { return p.x > -250 && p.x < extent.x + 250 && p.y > -80 && p.y < extent.y; };
    enum class Kind { Crystal, Environment, Decoration, Building, Corpse, Bones, Unit, RallyPoint };
    struct Item { float depth; Kind kind; size_t index; };
    std::vector<Item> items;
    std::vector<const Building*> groundSelections;
    for (const auto& b : game.buildings()) if ((ui.selection.contains(b.id) || !b.complete()) &&
        onScreen(view.project(center(b.origin), float(map.at(b.origin).height)))) groundSelections.push_back(&b);
    for (size_t i = 0; i < game.landscape().decorations.size(); ++i) {
        const auto p = game.landscape().decorations[i].position;
        if (game.fog().explored({int(p.x), int(p.y)})) items.push_back({p.y, Kind::Decoration, i});
    }
    for (size_t i = 0; i < game.crystals().size(); ++i) if (game.knownCrystal(i) > 0)
        items.push_back({game.crystals()[i].cell.y + .5f, Kind::Crystal, i});
    for (size_t i = 0; i < game.environment().size(); ++i) if (game.knownEnvironment(i)) {
        const auto& object = game.environment()[i];
        items.push_back({object.origin.y + object.height - .5f, Kind::Environment, i});
    }
    for (size_t i = 0; i < game.buildings().size(); ++i) items.push_back({buildingDepth(game.buildings()[i]), Kind::Building, i});
    for (size_t i = 0; i < game.buildings().size(); ++i) if (rallyPointVisible(game, game.buildings()[i], ui))
        items.push_back({game.buildings()[i].rally.y + .5f, Kind::RallyPoint, i});
    for (size_t i = 0; i < game.corpses().size(); ++i) if (game.fog().visible(game.corpses()[i].cell))
        items.push_back({unitDrawDepth(game.corpses()[i].position), Kind::Corpse, i});
    for (size_t i = 0; i < game.bones().size(); ++i) if (game.fog().visible(game.bones()[i].cell))
        items.push_back({unitDrawDepth(game.bones()[i].position), Kind::Bones, i});
    for (size_t i = 0; i < game.units().size(); ++i) if (game.fog().visible(game.units()[i].cell) && !airborne(game.units()[i].definition.movement))
        items.push_back({unitDrawDepth(game.units()[i].position), Kind::Unit, i});
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });
    size_t nextItem = 0;
    // Draw back-to-front by ground row, with all objects ordered by their ground anchor.
    // Foreground elevated terrain is allowed to occlude lower objects behind its edge.
    for (int y = 0; y < map.height(); ++y) {
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
                decoration(object, map, view);
                break;
            }
            case Kind::Crystal: {
                const auto& crystal = game.crystals()[item.index];
                const auto p = view.project(center(crystal.cell), float(map.at(crystal.cell).height));
                if (!onScreen(p)) break;
                worldOpacity_ = game.fog().visible(crystal.cell) ? 1.0f : .3f;
                if (ui.selection.contains(crystal.id) && game.fog().visible(crystal.cell)) {
                    drawSelectionRing(p + Vec2{0, -5} * view.zoom, 34, 16, view.zoom,
                        selectionColor(crystal.owner, game.player().id));
                }
                auto* emission = game.fog().visible(crystal.cell) ? spriteLights_.at(crystal_.Get()).highlights.Get() : nullptr;
                if (emission) drawLightGlow(p + Vec2{0, -22} * view.zoom, 46 * view.zoom, 0xbd8fff,
                    crystalPulse(crystal.id, game.clock().elapsedTicks()));
                sprite(crystal_.Get(), rect(0, 0, 128, 128), p + Vec2{-48, -76} * view.zoom, Vec2{96, 96} * view.zoom, false, 1, emission);
                break;
            }
            case Kind::Environment: {
                auto object = game.environment()[item.index];
                if (!onScreen(view.project(center(object.origin), float(map.at(object.origin).height)))) break;
                object.hitPoints = std::max(1, object.hitPoints); // Last known image in explored fog.
                worldOpacity_ = game.environmentVisible(item.index) ? 1.0f : .3f;
                environmentObject(object, map, view);
                break;
            }
            case Kind::Building: {
                const auto& b = game.buildings()[item.index];
                if (onScreen(view.project(center(b.origin), float(map.at(b.origin).height))))
                    buildingSprite(game, b, view, ui.selection.contains(b.id));
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
                if (onScreen(unitScreenAnchor(view, u.position, game.unitHeight(u)))) unitSprite(game, u, view, ui.selection.contains(u.id));
                break;
            }
            }
        }
    }
    worldOpacity_ = 1;
    std::vector<const Unit*> flyers;
    for (const auto& u : game.units()) if (airborne(u.definition.movement) && game.fog().visible(u.cell)) flyers.push_back(&u);
    std::stable_sort(flyers.begin(), flyers.end(), [](const Unit* a, const Unit* b) { return a->position.y < b->position.y; });
    for (const auto* u : flyers) unitSprite(game, *u, view, ui.selection.contains(u->id));
    drawProjectiles(game, view);
    if (grid) for (size_t i = 0; i < game.environment().size(); ++i) {
        const auto& object = game.environment()[i];
        if (!game.knownEnvironment(i)) continue;
        for (int y = 0; y < object.height; ++y) for (int x = 0; x < object.width; ++x) {
            const Cell c = object.origin + Cell{x, y};
            if (!game.fog().explored(c)) continue;
            const auto p = view.project(center(c), float(map.at(c).height));
            if (!onScreen(p)) continue;
            auto footprint = map.surfaceCorners(c, view);
            for (auto& corner : footprint) corner = p + (corner - p) * .85f;
            polygon(footprint, object.blocks(x, y) ? 0xe16d65 : 0x73ca91, .7f, false);
        }
    }
    if (grid) for (const auto& u : game.units()) if (ui.selection.contains(u.id)) {
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
