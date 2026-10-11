#include "TestSupport.hpp"
#include "rts/MinimapRaster.hpp"
#include "rts/UnitOcclusion.hpp"

namespace rts::tests {
void selectionTests(TestSuite& test, const TestContext& context) {
    test("Floating notifications isolate recipients and never backlog events hidden by fog", [] {
        Map map(20, 20);
        FogOfWar fog(20, 20);
        FloatingNotification message;
        message.position = {10.5f, 10.5f}; message.text = L"+7"; message.started = 100;
        message.recipient = 1;
        FloatingNotifications privateFeed;
        privateFeed.publish(message, 0, fog);
        require(privateFeed.entries().empty(), "Private income leaked to another player");
        privateFeed.publish(message, 1, fog);
        require(privateFeed.entries().size() == 1, "Recipient lost private feedback");
        require(message.opacity(100) == 1 && message.opacity(145) > 0 && message.opacity(145) < 1 &&
            message.opacity(160) == 0, "Popup does not fade and expire");
        privateFeed.expire(160); require(privateFeed.entries().empty(), "Popup queue retains expired entries");
        message.recipient.reset();
        FloatingNotifications publicFeed;
        publicFeed.publish(message, 0, fog);
        require(publicFeed.entries().empty(), "Black mask revealed a public notification");
        fog.update(map, std::array{VisionSource{{10, 10}, 2, false}});
        require(publicFeed.entries().empty(), "Previously hidden notification appeared on exploration");
        publicFeed.publish(message, 0, fog);
        require(publicFeed.entries().size() == 1 && message.visibleTo(0, fog), "Visible public notification missing");
        fog.update(map, {});
        require(fog.explored({10, 10}) && !message.visibleTo(0, fog), "Explored fog still shows public notifications");
        FloatingNotifications remembered;
        remembered.publish(message, 0, fog);
        require(remembered.entries().empty(), "Public notification entered queue through explored fog");
    });
    test("Enemy and neutral units can only be inspected singly and never become command recipients", [] {
        rts::EntityDefinition worker;
        worker.dayVision = worker.nightVision = 12;
        worker.maximumMana = 100;
        worker.abilities = {{1, "Dummy", "", 10, 150}};
        rts::Scenario scene{rts::Map(40, 32), {1, 1}, {8, 8}, {}};
        scene.extraWorkers = {{10, 8}};
        scene.units = {{worker.id, 1, {14, 8}}, {worker.id, 1, {18, 8}},
            {worker.id, rts::neutralPlayer, {14, 12}}, {worker.id, 1, {35, 25}}};
        auto game = makeTestSimulation(std::move(scene), {}, worker, {worker});
        const rts::WorldView view{{90, 70}, .8f};
        const auto point = [&](rts::EntityId id) {
            const auto b = rts::unitBounds(game, *game.unit(id), view);
            return rts::Vec2{b.x + b.width * .5f, b.y + b.height * .5f};
        };
        rts::GameplayUi ui;
        const auto friendly = game.worker().id, enemy = game.units()[2].id;
        for (size_t index : {size_t{2}, size_t{3}, size_t{4}}) {
            const auto& foreign = game.units()[index];
            ui.selection.ids = {friendly};
            ui.selection.click(game, view, point(foreign.id), true);
            require(ui.selection.ids == std::vector{foreign.id} && ui.selection.inspectedUnit(game) == &foreign &&
                !ui.selection.activeUnit(game) && ui.selection.groups(game).empty(), "Inspection mixed owners or hid foreign stats");
            for (size_t slot = 0; slot < rts::commandSlots; ++slot)
                require(!rts::unitCommandVisible(game, ui, slot) && !rts::abilityCommandAt(game, ui, slot), "Foreign command or ability was exposed");
            const auto state = foreign.state;
            require(!game.activateAbility(foreign.id, 1) && !game.order(ui.selection.ids, rts::OrderKind::Move, {20, 20}),
                "Inspected foreign unit accepted a command");
            game.stop(ui.selection.ids);
            require(foreign.mana == 100 && foreign.state == state && foreign.abilityCooldown(1) == 0, "Inspection changed foreign runtime state");
            ui.controlGroups.bind(0, game, ui.selection);
            require(ui.controlGroups.members(0).empty(), "Inspection entered an army control group");
            require(!ui.selection.selectTypeInView(game, view, {0, 0, 2000, 2000}, point(foreign.id), true) &&
                ui.selection.ids == std::vector{foreign.id}, "Foreign double click selected multiple units");
            ui.selection.click(game, view, point(friendly), true);
            require(ui.selection.ids == std::vector{friendly}, "Shift click mixed a friendly unit with inspection");
        }
        ui.selection.ids = {enemy};
        ui.selection.box(game, view, {0, 0, 2000, 2000}, true);
        require(ui.selection.ids == std::vector{friendly, game.units()[1].id}, "Box selection retained or added foreign units");
        const auto hidden = game.units().back().id;
        require(!rts::selectableEntity(game, hidden) && rts::pickEntity(game, view, point(hidden)) != hidden, "Hidden enemy was selectable");
        ui.selection.ids = {enemy};
        auto& fog = const_cast<rts::FogOfWar&>(game.fog());
        fog.update(game.map(), {});
        require(!ui.selection.inspectedUnit(game) && rts::pickEntity(game, view, point(enemy)) != enemy, "Inspection leaked through fog");
        ui.selection.prune(game);
        require(ui.selection.ids.empty(), "Enemy stayed selected after leaving vision");
    });
    test("Foreign building inspection uses the entire visible footprint and rejects commands", [] {
        auto game = makeTestSimulation({rts::Map(24, 24), {2, 2}, {7, 7}, {}});
        // Configure a foreign runtime fixture; authored maps currently only spawn the player's depot.
        auto& building = const_cast<rts::Building&>(game.buildings().front());
        auto& fog = const_cast<rts::FogOfWar&>(game.fog());
        const auto edge = building.origin + rts::Cell{building.definition.width - 1, building.definition.height - 1};
        fog.update(game.map(), std::array{rts::VisionSource{edge, 0, true}});
        require(!game.fog().visible(building.origin) && rts::buildingVisible(game, building), "Footprint visibility only checked the origin");
        const rts::WorldView view{{200, 180}, 1};
        const auto b = rts::buildingBounds(game, building, view);
        const rts::Vec2 point{b.x + b.width * .5f, b.y + b.height * .5f};
        rts::Selection selection;
        for (rts::PlayerId owner : {rts::PlayerId{1}, rts::neutralPlayer}) {
            building.owner = owner;
            selection.ids = {game.worker().id};
            selection.click(game, view, point, true);
            require(selection.ids == std::vector{building.id} && rts::selectableEntity(game, building.id), "Foreign building did not replace army selection");
            const auto rally = building.rally;
            const auto balance = game.storedCrystals();
            require(!game.train(building.id) && !game.setRally(building.id, {10, 10}) && !game.cancelTraining(building.id),
                "Foreign building accepted production or rally control");
            require(building.rally == rally && game.storedCrystals() == balance && building.production.empty(), "Foreign building command changed state");
            building.constructionProgress = 0;
            require(!game.cancelConstruction(building.id), "Foreign construction could be cancelled");
            game.command(std::array{game.worker().id}, building.origin);
            require(game.worker().state != rts::UnitState::ToBuild && game.worker().targetBuilding != building.id,
                "Player worker tried to finish foreign construction");
            building.constructionProgress = building.definition.constructionTicks;
        }
        fog.update(game.map(), {});
        require(!rts::selectableEntity(game, building.id) && rts::pickEntity(game, view, point) != building.id, "Hidden building stayed inspectable");
        selection.prune(game);
        require(selection.ids.empty(), "Building stayed selected after leaving vision");
        fog.update(game.map(), std::array{rts::VisionSource{edge, 0, true}});
        building.health = 0;
        require(!rts::selectableEntity(game, building.id) && rts::pickEntity(game, view, point) != building.id, "Destroyed building stayed selectable");
    });
    test("Numbered groups share members, replace independently and recall the full selection", [] {
        rts::EntityDefinition worker;
        auto soldier = worker; soldier.id = "soldier"; soldier.canBuild = false; soldier.carryCapacity = 0;
        auto scene = flatScenario();
        scene.units = {{soldier.id, 0, {5, 4}}, {soldier.id, 1, {8, 8}}};
        auto game = makeTestSimulation(std::move(scene), {}, worker, {worker, soldier});
        rts::Selection selection;
        const auto a = game.worker().id, b = game.units()[1].id, enemy = game.units()[2].id;
        selection.ids = {a, b, a, enemy, game.buildings().front().id, 99999};
        selection.activateGroup(game, a);
        rts::ControlGroups groups;
        for (size_t slot = 0; slot < rts::controlGroupCount; ++slot) groups.bind(slot, game, selection);
        for (size_t slot = 0; slot < rts::controlGroupCount; ++slot) {
            selection = {};
            require(groups.recall(slot, game, selection) && selection.ids.size() == 2 && selection.contains(a) && selection.contains(b),
                "Numbered group lost another active type, duplicated a unit or accepted a foreign/non-unit entity");
            std::reverse(selection.ids.begin(), selection.ids.end());
            require(groups.selected(slot, selection), "Group highlight depends on selection order");
        }
        selection.ids = {b}; groups.bind(0, game, selection);
        require(groups.members(0) == std::vector{b} && groups.members(9).size() == 2, "Rebinding one slot changed another group");
        selection = {}; groups.bind(0, game, selection);
        selection.ids = {a};
        require(!groups.recall(0, game, selection) && !groups.recall(10, game, selection) && selection.ids == std::vector{a},
            "An empty or invalid group cleared the current selection");
        groups.bind(10, game, selection);
        require(groups.members(9).size() == 2 && rts::controlGroupKey(9) == L'0', "Tenth slot or overlapping membership was lost");
    });
    test("Casualties disappear from every numbered group and empty bindings vanish", [] {
        rts::EntityDefinition worker; worker.maximumHealth = 1;
        auto killer = worker; killer.id = "killer"; killer.canBuild = false; killer.carryCapacity = 0;
        killer.attackDamage = 100; killer.attackWindupTicks = 1;
        rts::Scenario scene{rts::Map(40, 32), {3, 3}, {22, 20}, {}};
        scene.extraWorkers = {{7, 9}}; scene.units = {{killer.id, 1, {23, 20}}};
        auto game = makeTestSimulation(std::move(scene), {}, worker, {worker, killer});
        const auto victim = game.worker().id, survivor = game.units()[1].id;
        rts::Selection selection; selection.ids = {victim, survivor};
        rts::ControlGroups groups; groups.bind(0, game, selection);
        selection.ids = {victim}; groups.bind(9, game, selection);
        ticks(game, 20);
        require(!game.unit(victim), "Casualty fixture did not kill the worker");
        require(groups.recall(0, game, selection) && selection.ids == std::vector{survivor}, "Recall retained a dead group member");
        require(groups.members(0) == std::vector{survivor} && groups.members(9).empty(), "Death was not removed from all bindings");
        require(!groups.recall(9, game, selection) && selection.ids == std::vector{survivor}, "Wiped group remained selectable");
        require(rts::GameplayUi{}.controlGroups.members(0).empty(), "New match inherited a numbered group");
    });
    test("Active type controls visible commands while general orders retain every selected unit", [&] {
        const auto defs = rts::Definitions::load(context.assets / "data/catalog.json");
        auto types = defs.entities();
        auto secondWorker = defs.entity("church.worker"); secondWorker.id = "other.worker"; types.push_back(secondWorker);
        auto secondSiege = defs.entity("church.catapult"); secondSiege.id = "other.siege"; types.push_back(secondSiege);
        rts::Scenario scene{rts::Map(40, 32), {3, 3}, {7, 9}, {}};
        scene.units = {{"church.peacemaker", 0, {10, 10}}, {"church.catapult", 0, {11, 11}},
            {secondWorker.id, 0, {8, 9}}, {secondSiege.id, 0, {12, 11}}, {"church.peacemaker", 1, {25, 25}}};
        rts::Simulation game(std::move(scene), {}, defs.entity("church.worker"), types, defs.startingDepot(defs.entity("church.worker").factionId).id);
        rts::GameplayUi ui;
        for (const auto& unit : game.units()) if (unit.owner == game.player().id) ui.selection.ids.push_back(unit.id);
        const auto soldier = game.units()[1].id, siege = game.units()[2].id;
        const auto slot = [](rts::UnitCommand kind) {
            for (const auto& command : rts::unitCommands) if (command.command == kind) return command.slot;
            throw std::runtime_error("Missing command");
        };
        require(ui.selection.activeGroup(game).type == "church.peacemaker", "Soldier did not lead the non-hero selection");
        for (auto kind : {rts::UnitCommand::Move, rts::UnitCommand::Stop, rts::UnitCommand::Attack, rts::UnitCommand::Hold, rts::UnitCommand::Patrol})
            require(rts::unitCommandVisible(game, ui, slot(kind)), "Common soldier command hidden");
        for (auto kind : {rts::UnitCommand::Gather, rts::UnitCommand::Build, rts::UnitCommand::AttackGround})
            require(!rts::unitCommandVisible(game, ui, slot(kind)), "Another type leaked its command into the panel");
        for (auto kind : {rts::OrderKind::Move, rts::OrderKind::Stop, rts::OrderKind::AttackMove, rts::OrderKind::Hold, rts::OrderKind::Patrol})
            require(rts::commandRecipients(game, ui, kind) == ui.selection.ids, "Common order narrowed to active type");
        ui.selection.activateGroup(game, game.worker().id);
        require(rts::unitCommandVisible(game, ui, slot(rts::UnitCommand::Gather)) && rts::unitCommandVisible(game, ui, slot(rts::UnitCommand::Build)), "Worker commands missing");
        require(rts::commandRecipients(game, ui, rts::OrderKind::Gather) == std::vector{game.worker().id} &&
            rts::commandRecipients(game, ui, rts::OrderKind::Build) == std::vector{game.worker().id}, "Worker order reached another capable type");
        ui.buildMenu = true;
        require(rts::unitCommandVisible(game, ui, rts::backCommandSlot) && !rts::unitCommandVisible(game, ui, 8), "Back command did not use the last grid slot");
        ui.buildMenu = false;
        ui.selection.activateGroup(game, siege);
        require(rts::unitCommandVisible(game, ui, slot(rts::UnitCommand::AttackGround)) &&
            rts::commandRecipients(game, ui, rts::OrderKind::AttackGround) == std::vector{siege}, "Ground fire reached another siege type");
        require(!ui.selection.activateGroup(game, game.units().back().id), "Foreign unit became active");
        ui.selection.ids = {soldier}; ui.selection.prune(game);
        for (size_t i : {size_t{8}, size_t{9}, size_t{10}, size_t{12}, size_t{100}})
            require(!rts::unitCommandVisible(game, ui, i), "Empty or out-of-range slot exposed a command");
    });
    test("Idle worker cycling uses live free workers and skips holds, active orders and other owners", [] {
        auto scene = flatScenario(); scene.extraWorkers = {{5, 3}, {6, 3}};
        rts::EntityDefinition worker;
        auto soldier = worker; soldier.id = "soldier"; soldier.canBuild = false; soldier.carryCapacity = 0;
        scene.units = {{worker.id, 1, {8, 8}}, {soldier.id, 0, {5, 5}}};
        auto game = makeTestSimulation(std::move(scene), {}, worker, {worker, soldier});
        const auto a = game.units()[0].id, b = game.units()[1].id, c = game.units()[2].id;
        rts::Selection selection;
        require(selection.idleWorker(game) && selection.ids == std::vector{a}, "First idle worker wrong");
        require(selection.idleWorker(game, a) && selection.ids == std::vector{b}, "Second idle worker wrong");
        game.order(std::array{b}, rts::OrderKind::Hold);
        require(selection.idleWorker(game, a) && selection.ids == std::vector{c}, "Held worker entered idle pool");
        require(selection.idleWorker(game, c) && selection.ids == std::vector{a}, "Idle cycle did not wrap");
        game.order(std::array{a, c}, rts::OrderKind::Move, {8, 5}); game.tick();
        game.stop(std::array{a, c});
        require(selection.idleWorker(game, a), "Immediately stopped worker did not enter idle pool");
        ticks(game, 30);
        require(selection.idleWorker(game, b) && selection.ids == std::vector{c}, "Stopped worker did not return to the pool");
    });
    test("Minimap cache refreshes fog terrain and replacement maps but skips unchanged frames", [] {
        rts::Map map(16, 12);
        rts::FogOfWar fog(16, 12);
        rts::FogMask mask;
        const std::array<rts::VisionSource, 1> source{{{{8, 6}, 40, true}}};
        fog.update(map, source); mask.update(fog, 16, 12);
        rts::MinimapRaster raster;
        require(raster.update(map, fog, mask), "First image missing");
        const auto original = raster.pixels();
        require(!raster.update(map, fog, mask) && raster.pixels() == original, "Static frame rebuilt or changed");
        map.at({8, 6}).surface = rts::Surface::DeepWater;
        require(raster.update(map, fog, mask) && raster.pixels() != original, "In-place terrain change missed");
        const auto water = raster.pixels();
        map = rts::Map(16, 12); map.at({8, 6}).height = 1;
        require(raster.update(map, fog, mask) && raster.pixels() != water, "Same-size map replacement missed");
        const auto plateau = raster.pixels();
        fog.update(map, {}); mask.update(fog, 16, 12);
        require(raster.update(map, fog, mask) && raster.pixels() != plateau, "Fog transition missed");
        require(!raster.update(map, fog, mask), "Unchanged fog rebuilt");
        fog = rts::FogOfWar(16, 12); mask.update(fog, 16, 12);
        require(raster.update(map, fog, mask), "Exploration leaked across matches");
        const auto unexplored = raster.pixels();
        map = rts::Map(12, 16); fog = rts::FogOfWar(12, 16); mask.update(fog, 12, 16);
        require(raster.update(map, fog, mask) && raster.pixels() != unexplored, "Aspect ratio change missed");
        require(!raster.update(map, fog, mask), "Repeated rectangular map rebuilt");
    });
    test("Minimap shows precise obstacle footprints in explored fog without leaking through black mask", [] {
        Map map(20, 20); FogOfWar fog(20, 20); FogMask mask;
        auto tree = makeEnvironment("TREE", 1000, {7, 7});
        auto rock = makeEnvironment("ROCK", 1001, {11, 7});
        auto arch = makeEnvironment("ARCH", 1002, {7, 12});
        const std::array<const EnvironmentObject*, 3> known{{&tree, &rock, &arch}};
        map.at({13, 12}).blocked = true;
        map.at({15, 12}).surface = Surface::ShallowWater;
        map.at({16, 12}).surface = Surface::DeepWater;
        const std::array sources{VisionSource{{10, 10}, 30, true}};
        fog.update(map, sources); mask.update(fog, 20, 20);
        MinimapRaster raster; raster.update(map, fog, mask, known);
        const MinimapProjection projection({0, 0, 164, 164}, map);
        const auto sample = [&](Cell c) {
            const auto p = projection.project(center(c));
            return raster.pixels()[size_t(p.y) * 164 + size_t(p.x)];
        };
        const auto grass = sample({6, 7});
        require(sample({7, 7}) != grass && sample({11, 7}) != grass && sample({7, 7}) != sample({11, 7}),
            "Trees and rocks lack distinct muted obstacle colors");
        require(sample({8, 12}) == grass && sample({12, 8}) == grass && sample({7, 12}) != grass && sample({9, 12}) != grass,
            "Minimap painted the open arch or empty part of a collision footprint solid");
        require(sample({13, 12}) != grass && sample({15, 12}) != sample({16, 12}),
            "Blocked terrain or water depth is indistinguishable");
        const auto litTree = sample({7, 7});
        fog.update(map, {}); mask.update(fog, 20, 20); raster.update(map, fog, mask, known);
        require(sample({7, 7}) != litTree && sample({7, 7}) != sample({6, 7}) && sample({7, 7}) != 0xff081119,
            "Explored obstacle vanished or failed to dim in fog");
        fog = FogOfWar(20, 20);
        const std::array smallSource{VisionSource{{5, 7}, 2}};
        fog.update(map, smallSource); mask.update(fog, 20, 20); raster.update(map, fog, mask, known);
        require(!fog.explored({7, 7}) && mask.lightAt(center({7, 7})) > 0 && sample({7, 7}) == 0xff081119,
            "Feathering exposed an unexplored obstacle beside the visible area");
        require(sample({13, 12}) == 0xff081119, "Black mask leaked authored impassable terrain");
    });
    test("Crystal reserve updates on harvest and depletion removes selection", [] {
        auto game = makeTestSimulation(flatScenario(2));
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
    test("Occlusion windows share picking, camera scale and logical visibility", [] {
        Scenario s{Map(24, 24), {1, 1}, {3, 0}, {}};
        auto game = makeTestSimulation(std::move(s));
        const auto& unit = game.units()[0];
        const auto& building = game.buildings()[0];
        for (float zoom : {.35f, 1.f, 1.8f}) {
            WorldView view{{450, 300}, zoom};
            const auto window = unitOcclusion(game, unit, view);
            const Vec2 middle{window.body.x + window.body.width * .5f, window.body.y + window.body.height * .5f};
            require(window.contains(middle) && std::abs(window.opacity(middle) - .18f) < .001f, "Window body is opaque");
            const Vec2 rim{window.body.x - window.padding - window.feather * .5f, middle.y};
            require(!window.contains(rim) && window.opacity(rim) > .18f && window.opacity(rim) < 1, "Window has no soft fringe");
            require(window.opacity({rim.x - window.feather, rim.y}) == 1, "Window changed distant pixels");
            require(pickEntity(game, view, middle) == unit.id, "Camera changed selection through the building");
            const auto b = buildingBounds(game, building, view);
            require(pickEntity(game, view, {b.x + b.width - 2 * zoom, b.y + b.height * .5f}) == building.id,
                "Building outside window became unselectable");
            require(occlusionEligible(game, unit, buildingDepth(building)), "Visible rear unit has no mask");
            require(!occlusionEligible(game, unit, unitDrawDepth(unit.position)), "Unit masks an object behind itself");
        }
        auto hidden = unit; hidden.cell = {game.map().width()-1, game.map().height()-1};
        require(!game.fog().visible(hidden.cell), "Hidden-unit fixture is inside the sight radius");
        require(!occlusionEligible(game, hidden, 1000), "Hidden unit produces a mask");
        auto dead = unit; dead.health = 0;
        require(!occlusionEligible(game, dead, 1000), "Dead unit produces a mask");
        for (PlayerId owner : {game.player().id, PlayerId(1), neutralPlayer}) {
            auto other = unit; other.owner = owner;
            require(occlusionEligible(game, other, buildingDepth(building)), "Mask depends on allegiance");
        }
    });
}
}
