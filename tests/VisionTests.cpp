#include "TestSupport.hpp"
#include "rts/NightLighting.hpp"
#include "rts/MinimapRaster.hpp"

namespace rts::tests {
void visionTests(TestSuite& test, const TestContext& context) {
    test("Minimum sight respects cliffs and blockers while downward sight retains its radius", [&] {
        Map map(12,12); FogOfWar fog(12,12);
        for (int y=0;y<12;++y) map.at({6,y}).height=1;
        const std::array objects{context.worldAssets.instantiate("tree",1000,{5,6})};
        map.rebuildVisionBlockers(objects);
        for (int radius : {1,8}) {
            const VisionSource source{{5,5},radius};
            fog.update(map,std::span<const VisionSource>(&source,1));
            for (int y=4;y<=6;++y) for (int x=4;x<=6;++x) {
                const bool expected=x!=6;
                require(fog.visible({x,y})==expected && visionReaches(map,source,{x,y})==expected,
                    "Minimum sight revealed a higher neighbour or lost a same-level neighbour");
            }
            require(!fog.visible({7,5}) && !visionReaches(map,source,{7,5}), "Proximity sight revealed beyond a cliff");
            require(!fog.visible({5,7}) && !visionReaches(map,source,{5,7}), "Proximity sight revealed behind a tree");
        }
        const VisionSource high{{6,5},8};
        fog.update(map,std::span<const VisionSource>(&high,1));
        require(fog.visible({2,5}) && visionReaches(map,high,{2,5}), "Downward sight was limited to one cell");
        const VisionSource air{{5,5},1,true};
        fog.update(map,std::span<const VisionSource>(&air,1));
        require(fog.visible({6,5}) && visionReaches(map,air,{6,5}), "Cliff incorrectly blocked adjacent air sight");
        for (Cell edge : {Cell{0,0},{11,0},{0,11},{11,11}}) {
            const VisionSource source{edge,1};
            fog.update(map,std::span<const VisionSource>(&source,1));
            for (int y=0;y<12;++y) for (int x=0;x<12;++x)
                require(fog.visible({x,y}) == (std::abs(x-edge.x)<=1 && std::abs(y-edge.y)<=1),
                    "Minimum sight is not a clipped one-cell neighbourhood");
        }
    });
    test("Ramp movement keeps sight and selection every tick and reveals the plateau immediately", [] {
        for (Cell direction : {Cell{1,0},{-1,0},{0,1},{0,-1}}) for (int minute : {720,1320}) {
            const auto cell = [&](int along, int lane=0) {
                return Cell{20 + along*direction.x - lane*direction.y, 20 + along*direction.y + lane*direction.x};
            };
            Scenario scene{Map(40,40),{1,1},cell(-3),{}};
            for (int y=0;y<40;++y) for (int x=0;x<40;++x)
                if ((x-20)*direction.x+(y-20)*direction.y >= 1) scene.map.at({x,y}).height=1;
            for (int lane=-1;lane<=1;++lane) scene.map.at(cell(0,lane)).ramp=direction;
            EntityDefinition worker; worker.dayVision=5; worker.nightVision=1;
            auto depot=testDepot("hall",worker.id); depot.dayVision=depot.nightVision=1;
            auto game = makeTestSimulation(std::move(scene),{},worker,{depot},{},{},{.startMinute=minute});
            const auto id=game.worker().id;
            Selection selection; selection.ids={id};
            require(!game.fog().visible(cell(2)), "Plateau fixture already visible from below");
            bool enteredOffCadence=false, sawFromRamp=false;
            for (Cell goal : {cell(4),cell(-3)}) {
                require(game.order(std::array{id},OrderKind::Move,goal), "Ramp order failed");
                for (int tick=0;tick<300 && game.worker().state!=UnitState::Idle;++tick) {
                    const auto previous=game.worker().cell;
                    game.tick();
                    const auto current=game.worker().cell;
                    selection.prune(game);
                    require(game.fog().visible(current) && selectableEntity(game,id) && selection.ids==std::vector{id},
                        "Ramp transition hid the unit or dropped selection");
                    const auto& tile=game.map().at(current);
                    const int sightHeight=tile.height+(tile.ramp!=Cell{} ? 1 : 0);
                    for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
                        const Cell neighbour=current+Cell{x,y};
                        require(game.fog().visible(neighbour)==(game.map().at(neighbour).height<=sightHeight),
                            "Moving proximity sight ignored elevation or retained stale cells");
                    }
                    if (tile.ramp!=Cell{}) {
                        sawFromRamp=true;
                        require(game.fog().visible(cell(1)), "Ramp did not reveal the upper landing during ascent/descent");
                        if (minute==720) require(game.fog().visible(cell(4)), "Ramp did not grant the normal sight radius upstairs");
                    } else if (tile.height==0) {
                        require(!game.fog().visible(cell(1)), "Unit beside a ramp or back below it still sees the plateau");
                    }
                    if (game.map().at(previous).height==0 && game.map().at(current).height==1) {
                        enteredOffCadence |= game.clock().elapsedTicks()%5!=0;
                        if (minute==720) require(game.fog().visible(cell(4)), "Full plateau sight waited for a periodic update");
                    }
                }
                require(game.worker().cell==goal, "Unit did not finish the ramp crossing");
            }
            require(enteredOffCadence, "Fixture missed the gap between periodic fog updates");
            require(sawFromRamp, "Fixture never observed the ramp transition");
        }
    });
    test("Full map reveal persists without observers and resets for a new match", [&] {
        const auto definitions=Definitions::load(context.assets/"data/catalog.json");
        Scenario scenario{Map(32,32),{1,1},{4,3},{}};
        scenario.environment={context.worldAssets.instantiate("tree",1000,{25,25})};
        scenario.crystals={context.worldAssets.instantiateCrystal("crystal.small",{26,26})};
        scenario.units={{"church.peacemaker",1,{30,30}}};
        rebuildScenario(scenario,context.hallFootprint);
        Simulation game(scenario,{},definitions.entity("church.worker"),definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id);
        FogMask mask; mask.update(game.fog(),32,32);
        const auto revision=mask.revision();
        require(!game.fog().explored({30,30})&&!game.knownEnvironment(0)&&game.knownCrystal(0)==0,"Reveal fixture already explored");
        game.revealMap();
        require(mask.update(game.fog(),32,32)&&mask.revision()>revision&&mask.lightAt({28.5f,28.5f})==1,
            "Full reveal did not refresh the presentation mask");
        require(game.knownEnvironment(0)&&game.knownCrystal(0)>0&&game.environmentVisible(0),"Full reveal left hidden world records");
        game.revealMap();
        game.setEnvironmentHealth(1000,0); game.setEnvironmentHealth(1000,100);
        for(int i=0;i<30;++i) game.tick();
        for(int y=0;y<32;++y) for(int x=0;x<32;++x)
            require(game.fog().at({x,y})==Visibility::Visible,"Fog returned after a tick or blocker refresh");
        Simulation fresh(scenario,{},definitions.entity("church.worker"),definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id);
        require(!fresh.fog().explored({30,30}),"Reveal leaked into a new match");
        FogOfWar fog(32,32); fog.revealAll(); fog.update(scenario.map,{});
        require(fog.visible({31,31})&&fog.explored({0,0})&&!fog.visible({32,32}),"Observer-free reveal or map boundaries failed");
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
    test("Visible vineyard obstacle hides live entities behind it and respects memory and other observers", [&] {
        Scenario scene{Map(24, 24), {1, 1}, {4, 8}, {}};
        scene.environment = {context.worldAssets.instantiate("sunny_hills.vineyard.vine_a", 1000, {8, 8}),
            context.worldAssets.instantiate("sunny_hills.vineyard.barrel", 1001, {12, 8})};
        scene.crystals = {context.worldAssets.instantiateCrystal("crystal.small", {11, 9})};
        EntityDefinition worker; worker.dayVision = worker.nightVision = 18; worker.attackDamage = 10;
        auto depot = testDepot("hall", worker.id); depot.dayVision = depot.nightVision = 1;
        scene.units = {{worker.id, 1, {11, 8}}};
        rebuildScenario(scene, {depot.width, depot.height});
        auto game = makeTestSimulation(scene, {}, worker, {depot, worker});
        const auto enemy = game.units().back().id;
        const std::array attackers{game.worker().id};
        const auto hidden = [&] {
            require(game.environmentVisible(0) && game.knownEnvironment(0), "The first opaque object is not drawn");
            require(game.fog().visible({8, 8}) && !game.fog().visible({9, 8}), "Multi-cell blocker hid its front or exposed its back");
            require(!game.environmentVisible(1) && !game.crystalVisible(game.crystals().front()), "Live scenery leaked through the obstacle");
            require(!game.fog().visible({11, 8}) && !selectableEntity(game, enemy) && !game.attack(attackers, enemy),
                "Hidden enemy can be seen, selected or targeted");
        };
        hidden();
        require(!game.knownEnvironment(1) && game.knownCrystal(0) == 0, "Unexplored scenery leaked into rendering");
        require(game.setEnvironmentHealth(1000, 0), "Could not remove sight obstacle");
        require(game.environmentVisible(1) && game.crystalVisible(game.crystals().front()) &&
            selectableEntity(game, enemy) && game.attack(attackers, enemy), "Removing obstacle did not reveal live entities");
        require(game.setEnvironmentHealth(1000, scene.environment.front().maximumHitPoints), "Could not restore sight obstacle");
        hidden();
        require(game.fog().explored({11, 8}) && game.knownEnvironment(1) && game.knownCrystal(0) > 0,
            "Closing sight erased the remembered landscape instead of dimming it");
        FogMask memoryMask; memoryMask.update(game.fog(), 24, 24);
        MinimapRaster memoryMap; memoryMap.update(game, memoryMask);
        const auto remembered = memoryMap.pixels();
        require(game.setEnvironmentHealth(1001, 0) && game.knownEnvironment(1) && !game.environmentVisible(1),
            "Offscreen destruction removed the remembered world object");
        memoryMask.update(game.fog(), 24, 24);
        require(!memoryMap.update(game, memoryMask) && memoryMap.pixels() == remembered,
            "Minimap exposed destruction behind the sight blocker");
        require(game.setEnvironmentHealth(1000, 0) && !game.knownEnvironment(1),
            "Revisiting did not remove a destroyed object's remembered image");
        memoryMask.update(game.fog(), 24, 24);
        require(memoryMap.update(game, memoryMask) && memoryMap.pixels() != remembered,
            "Revisiting did not clear the obsolete minimap obstacle");
        auto flanked = scene; flanked.extraWorkers = {{11, 6}};
        auto flank = makeTestSimulation(std::move(flanked), {}, worker, {depot, worker});
        require(flank.fog().visible({11, 8}) && flank.environmentVisible(1), "One observer's obstacle cancelled another observer's sight");
        auto flyer = worker; flyer.movement = MovementType::Flying;
        auto air = makeTestSimulation(scene, {}, flyer, {depot, flyer});
        require(air.fog().visible({11, 8}) && air.environmentVisible(1), "Ground obstacle hid entities from an air observer");
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
        auto game = makeTestSimulation(std::move(s), {}, worker, {depot});
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
    test("Incremental fog pixels and upload region match a cold rebuild", [] {
        using namespace rts;
        FogMask mask;
        const auto verify = [&](const FogOfWar& fog, int width, int height) {
            const auto previous = mask.pixels();
            const auto revision = mask.revision();
            const bool changed = mask.update(fog, width, height);
            FogMask cold;
            cold.update(fog, width, height);
            require(mask.pixels() == cold.pixels(), "Partial fog rebuild left stale pixels or changed filtering");
            require(mask.revision() == revision + unsigned(changed), "Fog revision changed on a cache hit");
            const auto area = mask.changedRegion();
            for (int y = 0; y < mask.pixelHeight(); ++y) for (int x = 0; x < mask.pixelWidth(); ++x) {
                const size_t index = static_cast<size_t>(y) * mask.pixelWidth() + x;
                if (previous.size() == mask.pixels().size() && previous[index] != mask.pixels()[index])
                    require(x >= area.left && x < area.right && y >= area.top && y < area.bottom,
                        "A changed pixel fell outside the GPU upload region");
            }
            for (float y = -.5f; y < height + 1; y += .75f) for (float x = -.5f; x < width + 1; x += .75f)
                require(mask.lightAt({x, y}) == cold.lightAt({x, y}), "Partial fog light samples differ");
            if (!changed) require(area.left == area.right && area.top == area.bottom, "Cache hit retained a dirty region");
        };
        Map map(40, 28);
        FogOfWar fog(40, 28);
        verify(fog, 40, 28);
        // Edges, corners, overlapping and separated observers, radius changes,
        // explored transitions and replacement by another same-size map.
        std::mt19937 random(724);
        for (int step = 0; step < 45; ++step) {
            const std::array<VisionSource, 2> observers{{
                {{int(random() % 40), int(random() % 28)}, 1 + int(random() % 10), false},
                {{step % 2 ? 0 : 39, step % 3 ? 0 : 27}, 2, true}}};
            fog.update(map, observers); verify(fog, 40, 28);
            verify(fog, 40, 28);
        }
        fog.update(map, {}); verify(fog, 40, 28);
        fog = FogOfWar(40, 28); verify(fog, 40, 28);
        const VisionSource near{{20, 14}, 1};
        fog.update(map, std::span(&near, 1)); verify(fog, 40, 28);
        const auto area = mask.changedRegion();
        require((area.right - area.left) * (area.bottom - area.top) < mask.pixelWidth() * mask.pixelHeight() / 4,
            "A local visibility change rebuilt the full map");
        fog.revealAll(); verify(fog, 40, 28);
        FogOfWar swapped(28, 40); verify(swapped, 28, 40); // Same pixel count, different stride.
        FogOfWar tiny(1, 1); tiny.revealAll(); verify(tiny, 1, 1);
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
        auto game = makeTestSimulation(flatScenario());
        require(game.clock().phase() == rts::DayPhase::Day && game.clock().minuteOfDay() == 12 * 60,
            "Default match did not start at noon");
        const auto before = game.clock().elapsedTicks();
        game.command({6, 3});
        require(game.clock().elapsedTicks() == before, "Command advanced time while paused");
        game.tick();
        require(game.clock().elapsedTicks() == before + 1, "Simulation clock did not tick");
        mustThrow([] { rts::WorldClock invalid({30, 0, 0, 360, 1080}); });
    });
    test("Night reduces actual unit and building sight while retaining exploration", [] {
        auto s = flatScenario();
        rts::EntityDefinition type;
        type.dayVision = 3; type.nightVision = 1;
        auto depot = testDepot("hall", type.id, 60); depot.dayVision = 3; depot.nightVision = 1; depot.maximumHealth = 300;
        const std::vector<rts::EntityDefinition> buildings{depot};
        auto game = makeTestSimulation(std::move(s), {}, type, buildings, {}, {}, {.startMinute = 22 * 60});
        require(game.clock().phase() == rts::DayPhase::Night && !game.fog().visible({4, 5}), "Night radius missing at match start");
        ticks(game, 4800); // 22:00 -> 06:00 at 10 ticks per game minute.
        require(game.clock().phase() == rts::DayPhase::Day && game.fog().visible({4, 5}), "Day radius missing after dawn");
        ticks(game, 7200); // 06:00 -> 18:00.
        require(game.clock().phase() == rts::DayPhase::Night, "Night did not start");
        require(!game.fog().visible({4, 5}) && game.fog().explored({4, 5}), "Night radius or fog memory incorrect");
        require(game.fog().visible(game.worker().cell), "Observer cannot see own tile");
    });
}
}
