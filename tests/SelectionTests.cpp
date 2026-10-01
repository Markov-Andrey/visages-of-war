#include "TestSupport.hpp"
#include "rts/MinimapRaster.hpp"

namespace rts::tests {
void selectionTests(TestSuite& test, const TestContext& context) {
    test("Idle worker cycling uses live free workers and skips holds, pending orders and other owners", [] {
        auto scene = flatScenario(); scene.extraWorkers = {{5, 3}, {6, 3}};
        rts::EntityDefinition worker;
        auto soldier = worker; soldier.id = "soldier"; soldier.canBuild = false; soldier.carryCapacity = 0;
        scene.units = {{worker.id, 1, {8, 8}}, {soldier.id, 0, {5, 5}}};
        rts::Simulation game(std::move(scene), {}, worker, {worker, soldier});
        const auto a = game.units()[0].id, b = game.units()[1].id, c = game.units()[2].id;
        rts::Selection selection;
        require(selection.idleWorker(game) && selection.ids == std::vector{a}, "First idle worker wrong");
        require(selection.idleWorker(game, a) && selection.ids == std::vector{b}, "Second idle worker wrong");
        game.order(std::array{b}, rts::OrderKind::Hold);
        require(selection.idleWorker(game, a) && selection.ids == std::vector{c}, "Held worker entered idle pool");
        require(selection.idleWorker(game, c) && selection.ids == std::vector{a}, "Idle cycle did not wrap");
        game.order(std::array{a, c}, rts::OrderKind::Move, {8, 5}); game.tick();
        game.stop(std::array{a, c});
        require(!selection.idleWorker(game, a), "Moving workers with pending stop entered idle pool");
        ticks(game, 30);
        require(selection.idleWorker(game, b) && selection.ids == std::vector{c}, "Stopped worker did not return to the pool");
    });
    test("Rally points and links remain private even when a foreign building is selected", [&] {
        const auto defs = rts::Definitions::load(context.assets / "data/catalog.json");
        rts::Scenario scenario{rts::Map(28, 28), {1, 1}, {4, 3}, {}};
        rts::Simulation game(std::move(scenario), {}, defs.entity("human.worker"), defs.entities());
        auto building = game.buildings().front();
        building.rally = game.worker().cell;
        rts::GameplayUi ui; ui.selection.ids = {building.id};
        require(rts::rallyPointVisible(game, building, ui), "Selected owner's rally missing");
        building.owner = 1;
        require(!rts::rallyPointVisible(game, building, ui), "Visible selected enemy building exposed its rally");
        building.owner = rts::neutralPlayer;
        require(!rts::rallyPointVisible(game, building, ui), "Neutral building exposed its rally");
        building.owner = game.player().id;
        ui.selection.ids.clear();
        require(!rts::rallyPointVisible(game, building, ui), "Deselected building kept a rally marker");
        ui.selection.ids = {building.id};
        building.constructionProgress = 0;
        require(!rts::rallyPointVisible(game, building, ui), "Unfinished building exposed its rally");
        building.constructionProgress = building.definition.constructionTicks;
        building.health = 0;
        require(!rts::rallyPointVisible(game, building, ui), "Destroyed building retained its rally");
        building.health = building.definition.maximumHealth;
        building.rally = {game.map().width() - 1, game.map().height() - 1};
        require(!game.fog().explored(building.rally) && rts::rallyPointVisible(game, building, ui), "Own rally disappeared in unexplored terrain");
        require(!rts::rallyPointLightVisible(game, building, ui), "Unexplored rally emitted light");
        building.rally = {-1, -1};
        require(!rts::rallyPointVisible(game, building, ui), "Rally outside the map became visible");
        building.rally = game.worker().cell;
        building.definition.trainableUnits.clear();
        require(!rts::rallyPointVisible(game, building, ui), "Non-producing building retained its rally");
    });
    test("Authored building stages share drawing and picking bounds through camera changes", [&] {
        const auto definitions = rts::Definitions::load(context.assets / "data/catalog.json");
        auto scenario = flatScenario();
        for (int y = 0; y < context.hallFootprint.y; ++y) for (int x = 0; x < context.hallFootprint.x; ++x)
            scenario.map.at(scenario.hall + rts::Cell{x, y}).height = 2;
        rts::Simulation game(std::move(scenario), {}, definitions.entity("human.worker"), definitions.entities());
        auto building = game.buildings().front();
        const auto& art = building.definition.buildingSprite;
        for (float zoom : {.4f, 1.0f, 1.8f}) {
            const rts::WorldView view{{400, 450}, zoom};
            const auto ground = view.project({building.origin.x + building.definition.width * .5f, building.origin.y + building.definition.height * .5f}, 2);
            for (int progress : {0, 99, 198, 300}) {
                building.constructionProgress = progress;
                const auto* stage = art.stage(progress, building.definition.constructionTicks);
                const auto bounds = rts::buildingBounds(game, building, view);
                require(std::abs(bounds.width - stage->source[2] * art.scale * zoom) < .001f, "Picking width ignored sprite crop");
                require(std::abs(bounds.x + bounds.width * stage->anchor.x - ground.x) < .001f &&
                    std::abs(bounds.y + bounds.height * stage->anchor.y - ground.y) < .001f, "Stage anchor drifted from logical ground");
            }
            const auto bounds = rts::buildingBounds(game, game.buildings().front(), view);
            require(rts::pickEntity(game, view, {bounds.x + bounds.width * .5f, bounds.y + 12 * zoom}) == building.id, "New hall roof cannot be selected");
        }
    });
    test("Minimap cache refreshes fog terrain and replacement maps but skips unchanged frames", [] {
        rts::Map map(16, 12);
        rts::FogOfWar fog(16, 12);
        rts::FogMask mask;
        const std::array<rts::VisionSource, 1> source{{{{8, 6}, 40, true}}};
        fog.update(map, source); mask.update(fog, 16, 12);
        rts::MinimapRaster raster;
        require(raster.update(map, mask), "First image missing");
        const auto original = raster.pixels();
        require(!raster.update(map, mask) && raster.pixels() == original, "Static frame rebuilt or changed");
        map.at({8, 6}).surface = rts::Surface::DeepWater;
        require(raster.update(map, mask) && raster.pixels() != original, "In-place terrain change missed");
        const auto water = raster.pixels();
        map = rts::Map(16, 12); map.at({8, 6}).height = 1;
        require(raster.update(map, mask) && raster.pixels() != water, "Same-size map replacement missed");
        const auto plateau = raster.pixels();
        fog.update(map, {}); mask.update(fog, 16, 12);
        require(raster.update(map, mask) && raster.pixels() != plateau, "Fog transition missed");
        require(!raster.update(map, mask), "Unchanged fog rebuilt");
        fog = rts::FogOfWar(16, 12); mask.update(fog, 16, 12);
        require(raster.update(map, mask), "Exploration leaked across matches");
        const auto unexplored = raster.pixels();
        map = rts::Map(12, 16); fog = rts::FogOfWar(12, 16); mask.update(fog, 12, 16);
        require(raster.update(map, mask) && raster.pixels() != unexplored, "Aspect ratio change missed");
        require(!raster.update(map, mask), "Repeated rectangular map rebuilt");
    });
    test("Building sprite selection, box groups, additive selection and camera transforms", [] {
        auto s = flatScenario(); s.extraWorkers = {{5, 3}, {4, 4}};
        rts::Simulation game(std::move(s));
        rts::WorldView view{{500, 40}, 1.25f};
        rts::Selection selection;
        const auto bounds = rts::buildingBounds(game, game.buildings()[0], view);
        selection.click(game, view, {bounds.x + bounds.width * .5f, bounds.y + 30}, false);
        require(selection.ids.size() == 1 && selection.ids[0] == game.buildings()[0].id, "Building upper sprite not selectable");
        const auto p = view.project(game.worker().position, game.workerHeight()) + rts::Vec2{0, -25 * view.zoom};
        selection.click(game, view, p, false);
        require(selection.ids.size() == 1 && selection.ids[0] == game.worker().id, "Unit click missed");
        selection.click(game, view, p, true);
        require(selection.ids.empty(), "Shift click did not toggle");
        selection.box(game, view, {0, 0, 1200, 1200}, false);
        require(selection.ids.size() == 3, "Box selected building or missed units");
        selection.box(game, view, {0, 0, 1200, 1200}, true);
        require(selection.ids.size() == 3, "Additive box duplicated unit IDs");
    });
    test("Crystal sprite selection is neutral, single and excluded from army groups", [] {
        auto s = flatScenario(rts::Crystal::maximum); s.extraWorkers = {{5, 3}};
        rts::Simulation game(std::move(s));
        const auto& node = game.crystals().front();
        require(node.owner == rts::neutralPlayer && node.id && !game.unit(node.id) && !game.building(node.id), "Crystal has no independent neutral identity");
        require(game.crystal(node.id) == &node, "Crystal lookup lost identity");
        for (float zoom : {.4f, 1.0f, 1.8f}) {
            const rts::WorldView view{{170, -60}, zoom};
            const auto bounds = rts::crystalBounds(game, node, view);
            const rts::Vec2 crystalPoint{bounds.x + bounds.width * .5f, bounds.y + bounds.height * .25f};
            rts::Selection selection;
            selection.click(game, view, crystalPoint, false);
            require(selection.ids == std::vector<rts::EntityId>{node.id}, "Crystal upper sprite was not selected");
            require(!game.command(selection.ids, {6, 5}) && game.worker().state == rts::UnitState::Idle, "Neutral selection issued an order to player units");
            const auto workerPoint = view.project(game.worker().position, game.workerHeight()) + rts::Vec2{0, -25 * zoom};
            selection.click(game, view, workerPoint, true);
            require(selection.ids == std::vector<rts::EntityId>{game.worker().id}, "Shift mixed a crystal into a unit group");
            selection.click(game, view, crystalPoint, true);
            require(selection.ids == std::vector<rts::EntityId>{node.id}, "Crystal did not replace a unit group");
            selection.click(game, view, crystalPoint, true);
            require(selection.ids.empty(), "Shift did not toggle a crystal");
            selection.click(game, view, crystalPoint, false);
            selection.box(game, view, {-2000, -2000, 5000, 5000}, true);
            require(selection.ids.size() == game.units().size() && !selection.contains(node.id), "Box selection included a crystal");
            selection.click(game, view, crystalPoint, false);
            selection.army(game);
            require(selection.ids.empty(), "Army shortcut retained a crystal or workers");
        }
    });
    test("Crystal reserve updates on harvest and depletion removes selection", [] {
        rts::Simulation game(flatScenario(2));
        const auto nodeId = game.crystals().front().id;
        const auto cell = game.crystals().front().cell;
        const rts::WorldView view{{100, 100}, 1};
        const auto p = view.project(rts::center(cell)) + rts::Vec2{0, -25};
        rts::Selection selection;
        selection.click(game, view, p, false);
        require(selection.contains(nodeId), "Deposit selection failed");
        require(game.command(cell), "Gather order rejected");
        bool partial = false;
        for (int i = 0; i < 1200 && game.crystal(nodeId)->remaining; ++i) {
            game.tick();
            require(game.knownCrystal(0) == game.crystal(nodeId)->remaining, "Visible reserve lagged behind harvesting");
            partial |= game.crystal(nodeId)->remaining == 1;
            selection.prune(game);
            if (game.crystal(nodeId)->remaining) require(selection.contains(nodeId), "Harvesting lost resource selection");
        }
        require(partial && game.crystal(nodeId)->remaining == 0 && game.map().walkable(cell), "Deposit did not deplete normally");
        require(selection.ids.empty() && rts::pickEntity(game, view, p) != nodeId, "Depleted crystal remained selectable");
    });
    test("Crystals cannot be selected through unexplored or remembered fog", [] {
        auto s = flatScenario(); s.worker = {7, 5};
        s.crystals.push_back({{9, 9}, 1000}); s.map.occupy({9, 9});
        rts::EntityDefinition worker; worker.dayVision = worker.nightVision = 3;
        auto depot = testDepot("hall", worker.id); depot.dayVision = depot.nightVision = 1;
        rts::Simulation game(std::move(s), {}, worker, {depot});
        const auto nodeId = game.crystals()[0].id, hiddenId = game.crystals()[1].id;
        const rts::WorldView view{{40, 80}, 1};
        const auto p = view.project(rts::center({7, 7})) + rts::Vec2{0, -25};
        const auto hidden = view.project(rts::center({9, 9})) + rts::Vec2{0, -25};
        require(!game.fog().explored({9, 9}) && rts::pickEntity(game, view, hidden) != hiddenId, "Unknown crystal leaked through fog");
        rts::Selection selection;
        selection.click(game, view, p, false);
        require(selection.contains(nodeId), "Visible resource was not selectable");
        require(game.command({4, 1}), "Observer move failed"); ticks(game, 180);
        require(game.fog().explored({7, 7}) && !game.fog().visible({7, 7}) && game.knownCrystal(0) == 23, "Fog did not retain the resource image");
        selection.prune(game);
        require(selection.ids.empty(), "Resource selection survived losing sight");
        selection.click(game, view, p, false);
        require(!selection.contains(nodeId), "Remembered resource was selectable through fog");
        require(game.command({7, 5}), "Observer return failed"); ticks(game, 180);
        selection.click(game, view, p, false);
        require(selection.contains(nodeId), "Resource could not be selected after regaining sight");
    });
    test("Minimap shares square axes, picking and clipped camera bounds", [] {
        rts::Map map(64, 40);
        const rts::UiRect area{10, 20, 164, 164};
        const rts::MinimapProjection mini(area, map);
        const auto origin = mini.project({0, 0}), x = mini.project({1, 0}), y = mini.project({0, 1});
        require(x.x > origin.x && x.y == origin.y && y.x == origin.x && y.y > origin.y, "Minimap axes rotated or reflected");
        require(std::abs((x.x - origin.x) - (y.y - origin.y)) < .01f, "Minimap cells are not square");
        for (int cy = 0; cy < map.height(); ++cy) for (int cx = 0; cx < map.width(); ++cx)
            require(mini.pick(mini.project(rts::center({cx, cy}))) == rts::Cell{cx, cy}, "Minimap click missed displayed tile");
        require(!mini.pick({area.x + 1, area.y + 1}) && !mini.pick({area.x + 162, area.y + 162}), "Non-square map letterboxing accepted");
        const rts::MinimapProjection square(area, rts::Map(64, 64));
        require(square.pick({area.x + 1, area.y + 1}) == rts::Cell{0, 0} &&
                square.pick({area.x + 163, area.y + 163}) == rts::Cell{63, 63}, "Square map does not fill minimap corners");
        rts::WorldView camera{{0, 0}, .8f};
        const rts::UiRect world{0, 58, 1440, 638};
        camera.origin = rts::Vec2{720, 377} - camera.project({24, 20});
        const auto visible = mini.viewport(camera, world);
        require(visible.size() == 4, "Interior camera outline clipped incorrectly");
        require(std::abs(visible[0].y - visible[1].y) < .001f && std::abs(visible[1].x - visible[2].x) < .001f, "Camera outline is rotated");
        camera.origin = {720, 60};
        for (rts::Vec2 p : mini.viewport(camera, world)) {
            p = mini.unproject(p);
            require(p.x >= -.001f && p.y >= -.001f && p.x <= map.width() + .001f && p.y <= map.height() + .001f, "Outline escaped map");
        }
    });
    test("Locomotion uses step frames, retains facing and pauses with actual movement", [] {
        rts::Simulation game(flatScenario());
        game.command({8, 3});
        std::array<bool, 4> seen{};
        for (int tick = 0; tick < 25; ++tick) {
            game.tick();
            const auto frame = rts::locomotionFrame(game.worker());
            require(frame.column >= 0 && frame.column < 4, "Walk selected sword or other action frames");
            require(frame.row == 2, "East step did not face screen east");
            seen[frame.column] = true;
        }
        require(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }), "Walk never cycled through steps");
        game.stop(); ticks(game, 20);
        const auto stopped = rts::locomotionFrame(game.worker());
        const float phase = game.worker().walkCycle;
        ticks(game, 10);
        require(stopped.column == 0 && stopped.row == 2 && game.worker().walkCycle == phase, "Stopped unit walked or lost facing");
        rts::Unit waiting = game.worker(); waiting.route = {{0, 0}, {1, 0}}; waiting.next = 1; waiting.blockedTicks = 2;
        require(rts::locomotionFrame(waiting).column == 0, "Waiting unit walked in place");
    });
    test("Square projection picks slopes in all directions and rejects cliff faces", [] {
        for (int level : {-1, 0}) for (rts::Cell direction : {rts::Cell{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            rts::Map map(7, 7);
            for (int y = 0; y < 7; ++y) for (int x = 0; x < 7; ++x) map.at({x, y}).height = level;
            const rts::Cell low{3, 3}, high = low + direction;
            map.at(high).height = level + 1;
            map.at(low).ramp = direction;
            for (float zoom : {.4f, 1.0f, 1.8f}) {
                const rts::WorldView view{{170, -60}, zoom};
                for (float t : {.1f, .5f, .9f}) {
                    const auto world = rts::center(low) + rts::Vec2{float(direction.x), float(direction.y)} * (t - .5f);
                    require(map.pick(view.project(world, level + t), view) == low, "Ramp surface click is offset");
                }
                require(map.pick(view.project(rts::center(high), float(level + 1)), view) == high, "Plateau click is offset");
            }
        }
        rts::Map map(7, 7);
        map.at({3, 3}).height = 1;
        const rts::WorldView view{{50, 70}, 1.2f};
        require(!map.pick(view.project({3.5f, 4}, .5f), view), "Clicked through cliff face");
        require(map.pick(view.project({3.5f, 4.5f}, 0), view) == rts::Cell{3, 4}, "Ground below cliff became unselectable");
    });
    test("Square-view entity picking respects north-south sprite overlap", [] {
        auto s = flatScenario(); s.worker = {3, 0}; s.extraWorkers = {{3, 3}};
        rts::Simulation game(std::move(s));
        const rts::WorldView view{{300, 200}, 1};
        const auto back = view.project(game.units()[0].position, 0) + rts::Vec2{0, -25};
        require(rts::pickEntity(game, view, back) == game.buildings()[0].id, "Unit behind the building won selection by its x coordinate");
        const auto front = view.project(game.units()[1].position, 0) + rts::Vec2{0, -48};
        require(rts::pickEntity(game, view, front) == game.units()[1].id, "Foreground unit hidden from selection");
    });
}
}
