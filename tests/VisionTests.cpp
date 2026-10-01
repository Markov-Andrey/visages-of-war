#include "TestSupport.hpp"
#include "rts/NightLighting.hpp"

namespace rts::tests {
void visionTests(TestSuite& test, const TestContext& context) {
    test("Unexplored rally stays visible to its owner without revealing fog or emitting light", [] {
        rts::Scenario site{rts::Map(32, 24), {1, 1}, {4, 3}, {}}; site.startingCrystals = 100;
        rts::EntityDefinition worker; worker.trainingTicks = 1; worker.dayVision = worker.nightVision = 3;
        auto depot = testDepot("hall", worker.id); depot.dayVision = depot.nightVision = 3;
        rts::Simulation game(std::move(site), {}, worker, {depot});
        const auto hall = game.buildings().front().id;
        rts::GameplayUi ui; ui.selection.ids = {hall};
        const rts::Cell target{22, 18};
        const auto before = game.fog();
        require(rts::rallyPointLightVisible(game, *game.building(hall), ui), "Visible starting rally lost its light");
        require(!game.fog().explored(target) && game.setRally(hall, target), "Unexplored rally placement failed");
        require(game.building(hall)->rally == target && rts::rallyPointVisible(game, *game.building(hall), ui), "Placed rally has no owner marker");
        require(!rts::rallyPointLightVisible(game, *game.building(hall), ui), "Unexplored rally emitted light");
        ticks(game, 30);
        for (int y = 0; y < game.map().height(); ++y) for (int x = 0; x < game.map().width(); ++x)
            require(game.fog().at({x, y}) == before.at({x, y}), "Rally placement changed logical visibility");
        require(game.train(hall), "Could not train for an unexplored rally");
        game.tick();
        require(game.units().size() == 2 && game.units().back().cell == rts::Cell{1, 3} && !game.fog().explored(target),
            "Unexplored rally changed the exit or revealed its destination before arrival");
        ticks(game, 800);
        const auto trained = game.units().back().id;
        require(game.unit(trained)->cell == target && game.fog().visible(target), "Produced unit failed to reach and reveal its rally");
        require(rts::rallyPointLightVisible(game, *game.building(hall), ui), "Observed rally did not regain its light");
        require(game.command(std::array<rts::EntityId, 1>{trained}, {4, 6}), "Could not move the rally observer away");
        ticks(game, 800);
        require(game.fog().at(target) == rts::Visibility::Explored && rts::rallyPointVisible(game, *game.building(hall), ui),
            "Remembered rally kept vision or lost its marker");
        require(!rts::rallyPointLightVisible(game, *game.building(hall), ui), "Rally kept glowing after its observer left");
    });
    test("Crystal lights follow visible deposits and disappear on depletion without revealing fog", [] {
        rts::Scenario site{rts::Map(24, 20), {1, 1}, {9, 9}, {{{10, 9}, 1}, {{22, 18}, 1000}}};
        for (const auto& crystal : site.crystals) site.map.occupy(crystal.cell);
        rts::EntityDefinition worker; worker.dayVision = worker.nightVision = 4;
        auto depot = testDepot("hall", worker.id); depot.dayVision = depot.nightVision = 1;
        rts::Simulation game(site, {}, worker, {depot});
        const rts::WorldView view{{0, 0}, 1};
        const auto lights = rts::crystalLights(game, view);
        require(lights.size() == 1, "Hidden crystal emitted light");
        const auto moved = rts::crystalLights(game, {{17, 23}, .5f});
        require(moved[0].position == lights[0].position * .5f + rts::Vec2{17, 23} && moved[0].radius == lights[0].radius * .5f,
            "Crystal light detached from camera");
        rts::FogMask fog; fog.update(game.fog(), 24, 20);
        rts::NightLightingRaster raster; raster.update(game, view, {1536, 1280}, fog);
        const auto sample = [&](rts::Vec2 p) { return raster.pixels().at(size_t(int(p.y / rts::NightLightingRaster::pixelStep)) * raster.width() + int(p.x / rts::NightLightingRaster::pixelStep)); };
        const auto lit = sample(lights[0].position);
        const auto dark = sample({1400, 1150});
        require((lit >> 24) < (dark >> 24) && !game.fog().explored({22, 18}), "Crystal lighting did not weaken night or revealed fog");
        const auto paused = raster.pixels(); raster.update(game, view, {1536, 1280}, fog);
        require(paused == raster.pixels(), "Paused crystal light changed");
        game.command(site.crystals[0].cell);
        ticks(game, 120);
        require(game.crystals()[0].remaining == 0 && rts::crystalLights(game, view).empty(), "Exhausted crystal kept its glow");
        raster.update(game, view, {1536, 1280}, fog);
        require(sample(lights[0].position) == dark, "Exhausted crystal left a cached glow");
        rts::Simulation remembered(site, {}, worker, {depot});
        remembered.command({3, 9}); ticks(remembered, 100);
        require(remembered.fog().explored({10, 9}) && !remembered.fog().visible({10, 9}) &&
            rts::crystalLights(remembered, view).empty(), "Remembered crystal exposed live glow");
    });
    test("Building light follows construction and training without changing vision", [&] {
        const auto definitions = rts::Definitions::load(context.assets / "data/catalog.json");
        rts::Scenario site{rts::Map(28, 28), {8, 11}, {11, 11}, {}};
        site.startingCrystals = 500;
        rts::Simulation game(site, {}, definitions.entity("human.worker"), definitions.entities());
        const auto hall = game.buildings().front().id;
        const rts::WorldView view{{0, 0}, 1};
        const auto lights = rts::buildingLights(game, view);
        require(lights.size() == 2, "Idle hall needs ground and crown lights");
        const auto moved = rts::buildingLights(game, {{37, -19}, 2});
        const auto expected = lights[0].position * 2 + rts::Vec2{37, -19};
        require(moved.size() == lights.size() && std::abs(moved[0].position.x - expected.x) < .001f && std::abs(moved[0].position.y - expected.y) < .001f &&
            moved[0].radius == lights[0].radius * 2, "Camera detached light from building");
        require(game.train(hall) && rts::buildingLights(game, view).size() == 4, "Training did not light braziers");
        require(game.cancelTraining(hall) && rts::buildingLights(game, view).size() == 2, "Cancelled braziers still cast light");
        const std::array builders{game.worker().id};
        const auto building = game.construct(builders, "human.hall", {12, 11});
        require(building && rts::buildingLights(game, view).size() == 2, "Unfinished hall emitted light");
        ticks(game, 400);
        require(game.building(*building)->complete() && rts::buildingLights(game, view).size() == 4, "Finished hall remained dark");
        auto unlitTypes = definitions.entities();
        for (auto& type : unlitTypes) for (auto& stage : type.buildingSprite.stages) stage.lights.clear();
        rts::Simulation lit(site, {}, definitions.entity("human.worker"), definitions.entities());
        rts::Simulation unlit(site, {}, definitions.entity("human.worker"), unlitTypes);
        for (int y = 0; y < 28; ++y) for (int x = 0; x < 28; ++x)
            require(lit.fog().at({x, y}) == unlit.fog().at({x, y}) && lit.map().walkable({x, y}) == unlit.map().walkable({x, y}), "Light changed logical vision or navigation");
        require(rts::buildingLights(unlit, view).empty(), "Unconfigured building emitted light");
    });
    test("Night lighting fades smoothly, respects fog and stays stable while paused", [&] {
        const auto definitions = rts::Definitions::load(context.assets / "data/catalog.json");
        rts::Scenario site{rts::Map(28, 28), {8, 11}, {11, 11}, {}};
        rts::Simulation game(site, {}, definitions.entity("human.worker"), definitions.entities());
        const rts::WorldView view{{0, 0}, 1}; const rts::Vec2 extent{1800, 1800};
        rts::FogMask fog; fog.update(game.fog(), 28, 28);
        rts::NightLightingRaster raster; raster.update(game, view, extent, fog);
        const auto sample = [&](rts::Vec2 p) { return raster.pixels().at(size_t(int(p.y / rts::NightLightingRaster::pixelStep)) * raster.width() + int(p.x / rts::NightLightingRaster::pixelStep)); };
        const auto light = rts::buildingLights(game, view).front();
        const auto nearby = sample(light.position), edge = sample(light.position + rts::Vec2{light.radius * .65f, 0}), far = sample(light.position + rts::Vec2{light.radius * 1.1f, 0});
        require((nearby >> 24) < (edge >> 24) && (edge >> 24) < (far >> 24), "Night did not fade away smoothly around the light");
        require(sample({20, 20}) == far, "Light brightened unexplored ground");
        const auto paused = raster.pixels(); raster.update(game, view, extent, fog);
        require(raster.pixels() == paused, "Paused light flickered without simulation ticks");
        for (const auto pixel : raster.pixels()) {
            const auto alpha = pixel >> 24;
            require((pixel & 255) <= alpha && ((pixel >> 8) & 255) <= alpha && ((pixel >> 16) & 255) <= alpha, "Lighting bitmap lost premultiplied alpha");
        }
        // Remembered terrain keeps the normal night veil even within a light radius.
        rts::FogOfWar remembered(28, 28); const std::array source{rts::VisionSource{{9, 12}, 20}};
        remembered.update(site.map, source); remembered.update(site.map, {});
        fog.update(remembered, 28, 28); raster.update(game, view, extent, fog);
        require(sample(light.position) == far, "Lighting removed fog from remembered terrain");
        require(rts::nightStrength(rts::WorldClock({30, 480, 12 * 60, 360, 1080})) == 0 &&
            std::abs(rts::nightStrength(rts::WorldClock({30, 480, 6 * 60, 360, 1080})) - .5f) < .001f, "Daylight transition jumped");
    });
    test("Overlapping observers match a per-cell ray union across movement and terrain changes", [] {
        rts::Map map(28, 24);
        std::mt19937 random(92143);
        std::vector<rts::Visibility> expected(28 * 24, rts::Visibility::Unexplored);
        rts::FogOfWar fog(28, 24);
        for (int frame = 0; frame < 12; ++frame) {
            std::vector<rts::EnvironmentObject> trees;
            for (int i = 0; i < 30; ++i) {
                const rts::Cell cell{int(random() % 28), int(random() % 24)};
                map.at(cell).height = int(random() % 3);
                trees.push_back(rts::makeEnvironment("TREE", i + 1, cell));
            }
            map.rebuildVisionBlockers(trees);
            std::vector<rts::VisionSource> observers;
            // Includes fully overlapping sources, different heights and air vision.
            for (int i = 0; i < 16; ++i)
                observers.push_back({{int(random() % 28), int(random() % 24)}, 3 + int(random() % 8), i % 4 == 0});
            const std::vector<rts::VisionSource> duplicates(observers.begin(), observers.begin() + 4);
            observers.insert(observers.end(), duplicates.begin(), duplicates.end());
            if (frame == 11) observers.clear();
            fog.update(map, observers);
            for (int y = 0; y < 24; ++y) for (int x = 0; x < 28; ++x) {
                auto& value = expected[y * 28 + x];
                if (value == rts::Visibility::Visible) value = rts::Visibility::Explored;
                for (const auto observer : observers)
                    if (rts::visionReaches(map, observer, {x, y})) value = rts::Visibility::Visible;
                require(fog.at({x, y}) == value, "Visibility union or exploration changed");
            }
            const auto first = fog;
            std::reverse(observers.begin(), observers.end());
            fog.update(map, observers);
            for (int y = 0; y < 24; ++y) for (int x = 0; x < 28; ++x)
                require(fog.at({x, y}) == first.at({x, y}), "Observer order changes visibility");
        }
    });
    test("Arch opening is passable; destruction removes only its own collision", [] {
        auto scenario = flatScenario();
        auto arch = rts::makeEnvironment("ARCH", 1000, {4, 5});
        auto tree = rts::makeEnvironment("TREE", 1001, {4, 5});
        scenario.environment = {arch, tree};
        for (const auto& object : scenario.environment)
            for (int y = 0; y < object.height; ++y) for (int x = 0; x < object.width; ++x)
                if (object.blocks(x, y)) scenario.map.occupy(object.origin + rts::Cell{x, y});
        scenario.map.at({6, 5}).blocked = true; // Terrain must survive object destruction.
        rts::Simulation game(std::move(scenario));
        const auto path = rts::findPath(game.map(), {5, 4}, {5, 6});
        require(path && path->cost == 20, "Arch opening blocked by visual extent");
        require(!game.map().walkable({4, 5}), "Arch pillar not blocking");
        require(game.damageEnvironment(1000, 100), "Damage rejected");
        require(game.environment()[0].hitPoints == 400 && game.map().occupancy({4, 5}) == 2, "Partial damage released collision");
        require(game.damageEnvironment(1000, 400), "Destruction rejected");
        require(!game.map().walkable({4, 5}) && game.map().occupancy({4, 5}) == 1, "Destroyed another object's footprint");
        require(!game.map().walkable({6, 5}), "Destroyed terrain obstacle");
        require(!game.damageEnvironment(1000, 50), "Destroyed object damaged twice");
        const auto events = game.takeEvents();
        require(events.size() == 1 && std::get<rts::ObjectDestroyed>(events[0]).id == 1000, "Missing destruction event");
        game.damageEnvironment(1001, 100);
        require(game.map().walkable({4, 5}), "Tree removal did not open path");
    });
    test("Forest and rock faces are visible but stop ground sight; air sees over them", [] {
        rts::Map map(16, 16);
        std::vector<rts::EnvironmentObject> forest;
        for (int y = 0; y < 16; ++y) forest.push_back(rts::makeEnvironment("TREE", 1000 + y, {7, y}));
        map.rebuildVisionBlockers(forest);
        const rts::VisionSource ground{{3, 8}, 12};
        require(rts::visionReaches(map, ground, {7, 8}), "The blocking tree itself is hidden");
        require(!rts::visionReaches(map, ground, {8, 8}) && !rts::visionReaches(map, ground, {11, 12}), "Sight leaked through a solid forest");
        require(rts::visionReaches(map, {{3, 8}, 12, true}, {11, 12}), "Forest blocked air vision");
        map.at({7, 8}).height = 3;
        require(rts::visionReaches(map, {{3, 8}, 12, true}, {11, 8}), "High ground blocked air vision");
        require(!rts::visionReaches(map, {{3, 8}, 2, true}, {11, 8}), "Air vision ignored its radius");
        rts::Map corners(10, 10);
        std::vector<rts::EnvironmentObject> blockers{rts::makeEnvironment("TREE", 1, {4, 3}), rts::makeEnvironment("TREE", 2, {3, 4})};
        corners.rebuildVisionBlockers(blockers);
        require(!rts::visionReaches(corners, {{3, 3}, 8}, {5, 5}), "Sight slipped through touching tree corners");
        require(!rts::visionReaches(corners, {{5, 5}, 8}, {3, 3}), "Corner occlusion depends on ray direction");
        require(rts::visionReaches(corners, {{3, 3}, 8}, {4, 3}), "Adjacent tree face was hidden");
        blockers = {rts::makeEnvironment("ROCK", 3, {6, 5})};
        corners.rebuildVisionBlockers(blockers);
        require(!corners.blocksVision({7, 6}) && corners.blocksVision({6, 6}), "Rock sight ignored its partial footprint");
        require(rts::visionReaches(corners, {{8, 6}, 8}, {6, 6}) && !rts::visionReaches(corners, {{8, 6}, 8}, {5, 6}), "Rock face or shadow visibility incorrect");
        corners.rebuildVisionBlockers({}); corners.occupy({5, 6});
        require(rts::visionReaches(corners, {{8, 6}, 8}, {3, 6}), "Movement occupancy incorrectly blocks sight");
    });
    test("Tree health preserves identity and restores collision and sight without double counting", [] {
        auto s = flatScenario(); s.worker = {3, 5};
        s.environment = {rts::makeEnvironment("TREE", 1000, {5, 5}), rts::makeEnvironment("TREE", 1001, {5, 5})};
        s.map.occupy({5, 5}); s.map.occupy({5, 5});
        rts::EntityDefinition worker; worker.dayVision = worker.nightVision = 7;
        auto depot = testDepot("hall", worker.id); depot.dayVision = depot.nightVision = 1;
        rts::Simulation game(std::move(s), {}, worker, {depot});
        require(game.environment()[0].kind == rts::EnvironmentKind::Tree && game.environment()[0].maximumHitPoints == 100, "Tree type or health missing");
        require(game.fog().visible({5, 5}) && !game.fog().visible({7, 5}), "Tree does not shadow the area behind it");
        require(game.setEnvironmentHealth(1000, 0) && game.map().occupancy({5, 5}) == 1 && game.map().blocksVision({5, 5}), "Removed another object's blockers");
        require(game.setEnvironmentHealth(1000, 0) && game.map().occupancy({5, 5}) == 1, "Repeated zero health released collision twice");
        require(game.setEnvironmentHealth(1001, 0) && game.map().walkable({5, 5}) && game.fog().visible({7, 5}), "Felling did not immediately open path and sight");
        require(game.environment().size() == 2 && game.environment()[0].id == 1000 && !game.environment()[0].active(), "Dead tree was erased");
        require(game.setEnvironmentHealth(1000, 50) && game.environment()[0].active() && game.map().occupancy({5, 5}) == 1, "Positive health did not restore tree");
        require(!game.fog().visible({7, 5}) && game.fog().explored({7, 5}), "Restored tree did not close sight while retaining exploration");
        require(game.setEnvironmentHealth(1000, 80) && game.map().occupancy({5, 5}) == 1, "Healing duplicated collision");
        require(!game.setEnvironmentHealth(1000, -1) && !game.setEnvironmentHealth(9999, 50), "Invalid restoration was accepted");
        const auto events = game.takeEvents();
        require(events.size() == 3 && std::holds_alternative<rts::ObjectRestored>(events.back()), "Lifecycle events were lost or duplicated");
    });
    test("Fog presentation feathers a circular boundary without changing logical visibility", [] {
        rts::Map map(20, 20); rts::FogOfWar fog(20, 20); rts::FogMask mask;
        const std::array<rts::VisionSource, 1> sources{{{{8, 8}, 5}}};
        fog.update(map, sources);
        require(mask.update(fog, 20, 20) && !mask.update(fog, 20, 20), "Unchanged fog rebuilt its mask");
        require(mask.lightAt({8.5f, 8.5f}) > .99f && mask.lightAt({.5f, .5f}) == 0, "Fog centre or unexplored field is wrong");
        const float edge = mask.lightAt({13.5f, 8.5f});
        require(edge > .1f && edge < .95f && std::abs(edge - mask.lightAt({8.5f, 13.5f})) < .0001f, "Feathered circle is not symmetric");
        float previous = mask.lightAt({12, 8.5f});
        for (int i = 1; i <= 90; ++i) {
            const float next = mask.lightAt({12 + i / 30.0f, 8.5f});
            require(next <= previous + .0001f && previous - next < .06f, "Fog edge has a hard step or ringing");
            previous = next;
        }
        require(!fog.visible({14, 8}) && mask.lightAt({14, 8.5f}) > 0, "Visual feather changed game visibility");
        for (auto pixel : mask.pixels()) {
            const auto alpha = pixel >> 24;
            require((pixel & 255) <= alpha && ((pixel >> 8) & 255) <= alpha && ((pixel >> 16) & 255) <= alpha, "Fog bitmap is not premultiplied");
        }
        fog.update(map, {}); mask.update(fog, 20, 20);
        require(std::abs(mask.lightAt({8.5f, 8.5f}) - .3f) < .0001f && !fog.visible({8, 8}), "Explored fog did not dim");
        rts::FogOfWar fresh(20, 20); mask.update(fresh, 20, 20);
        require(mask.lightAt({8.5f, 8.5f}) == 0 && !mask.covers({8, 8}), "A new match reused old exploration");
    });
    test("Day/night transitions, midnight and pause use simulation ticks", [] {
        rts::WorldClock clock({30, 48, 359, 360, 1080}); // One tick per game minute.
        const auto dawn = clock.tick();
        require(dawn && dawn->after == rts::DayPhase::Day && dawn->minute == 360, "Sunrise event");
        for (int i = 0; i < 719; ++i) require(!clock.tick(), "Unexpected phase event");
        const auto dusk = clock.tick();
        require(dusk && dusk->after == rts::DayPhase::Night && dusk->minute == 1080, "Sunset event");
        rts::WorldClock midnight({30, 48, 1439, 360, 1080});
        require(!midnight.tick() && midnight.minuteOfDay() == 0, "Midnight wraps incorrectly");
        rts::Simulation game(flatScenario());
        require(game.clock().phase() == rts::DayPhase::Night && game.clock().minuteOfDay() == 22 * 60,
            "Default match did not start at 22:00");
        const auto before = game.clock().elapsedTicks();
        game.command({6, 3});
        require(game.clock().elapsedTicks() == before, "Command advanced time while paused");
        game.tick();
        require(game.clock().elapsedTicks() == before + 1, "Simulation clock did not tick");
        mustThrow([] { rts::WorldClock invalid({30, 0, 0, 360, 1080}); });
    });
    test("Fog combines observers, remembers exploration and respects higher ground", [] {
        rts::Map map(20, 20);
        for (int y = 0; y < 20; ++y) map.at({8, y}).height = 1;
        rts::FogOfWar fog(20, 20);
        const std::array<rts::VisionSource, 2> sources{{{{5, 5}, 7}, {{16, 16}, 2}}};
        fog.update(map, sources);
        require(fog.visible({5, 5}) && fog.visible({16, 17}), "Vision sources did not combine");
        require(!fog.visible({8, 5}) && !fog.visible({10, 5}), "Saw onto or through higher ground");
        require(!fog.explored({19, 0}), "Unexplored area revealed");
        fog.update(map, {});
        require(fog.at({5, 5}) == rts::Visibility::Explored && !fog.visible({5, 5}), "Exploration was lost");
        const std::array<rts::VisionSource, 1> high{{{{8, 5}, 4}}};
        fog.update(map, high);
        require(fog.visible({10, 5}) && fog.visible({6, 5}), "Higher observer cannot see lower terrain");
    });
    test("Night reduces actual unit and building sight while retaining exploration", [] {
        auto s = flatScenario();
        rts::EntityDefinition type;
        type.dayVision = 3; type.nightVision = 1;
        auto depot = testDepot("hall", type.id, 60); depot.dayVision = 3; depot.nightVision = 1; depot.maximumHealth = 300;
        const std::vector<rts::EntityDefinition> buildings{depot};
        rts::Simulation game(std::move(s), {}, type, buildings);
        require(game.clock().phase() == rts::DayPhase::Night && !game.fog().visible({4, 6}), "Night radius missing at match start");
        ticks(game, 4800); // 22:00 -> 06:00 at 10 ticks per game minute.
        require(game.clock().phase() == rts::DayPhase::Day && game.fog().visible({4, 6}), "Day radius missing after dawn");
        ticks(game, 7200); // 06:00 -> 18:00.
        require(game.clock().phase() == rts::DayPhase::Night, "Night did not start");
        require(!game.fog().visible({4, 6}) && game.fog().explored({4, 6}), "Night radius or fog memory incorrect");
        require(game.fog().visible(game.worker().cell), "Observer cannot see own tile");
    });
}
}
