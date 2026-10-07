#include "TestSupport.hpp"
#include "rts/MinimapRaster.hpp"
#include "rts/UnitOcclusion.hpp"

namespace rts::tests {
void selectionTests(TestSuite& test, const TestContext& context) {
    test("Hover previews the visible click target for every owner without selecting through UI or fog", [] {
        EntityDefinition worker; worker.dayVision = worker.nightVision = 20;
        Scenario scene{Map(40, 32), {1, 1}, {8, 8}, {}};
        scene.units = {{worker.id, 1, {12, 8}}, {worker.id, neutralPlayer, {16, 8}}};
        Simulation game(std::move(scene), {}, worker, {worker});
        GameplayUi ui; ui.selection.ids = {game.worker().id};
        const auto selection = ui.selection.ids;
        const BattleLayout layout({1280, 900});
        WorldView view{{}, .8f};
        for (const auto& unit : game.units()) {
            view.origin = Vec2{650, 350} - view.project(unit.position, game.unitHeight(unit));
            const auto b = unitBounds(game, unit, view);
            ui.mouse = {b.x + b.width * .5f, b.y + b.height * .5f};
            require(hoveredUnit(game, view, ui, layout) == unit.id && pickEntity(game, view, ui.mouse) == unit.id,
                "Hover did not match the visible unit under left click");
            require(ui.selection.ids == selection, "Hover changed actual selection");
            ui.drag = UiRect{0, 0, 20, 20};
            require(!hoveredUnit(game, view, ui, layout), "Selection drag retained a hover ring"); ui.drag.reset();
            ui.orderMode = OrderKind::AttackMove;
            require(!hoveredUnit(game, view, ui, layout), "Targeting cursor previewed a left-click selection"); ui.orderMode.reset();
        }
        const Vec2 minimap{layout.minimap.x + 40, layout.minimap.y + 40};
        view.origin = view.origin + minimap - ui.mouse; ui.mouse = minimap;
        require(pickEntity(game, view, ui.mouse).has_value() && !hoveredUnit(game, view, ui, layout), "Hovered through the raised minimap");
        const Vec2 world{650, 300}; view.origin = view.origin + world - ui.mouse; ui.mouse = world;
        require(hoveredUnit(game, view, ui, layout) != 0, "Hover did not resume outside the HUD");
        const_cast<FogOfWar&>(game.fog()).update(game.map(), {});
        require(!hoveredUnit(game, view, ui, layout), "Hover revealed a unit in logical fog");
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
        rts::Simulation game(std::move(scene), {}, worker, {worker});
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
        rts::Simulation game({rts::Map(24, 24), {2, 2}, {7, 7}, {}});
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
            const int balance = game.storedCrystals();
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
    test("HUD notifications expire after four real seconds and repeated messages restart the timer", [] {
        using namespace std::chrono;
        const rts::GameplayNotification::Clock::time_point began{};
        rts::Simulation game({rts::Map(16, 16), {1, 1}, {5, 5}, {}});
        rts::GameplayNotification notice;
        require(notice.text(game, began).empty(), "Map start displayed an unsolicited notification");
        const auto rejectOrder = [&] { require(!game.order({}, rts::OrderKind::Move, {10, 10}), "Empty selection accepted an order"); };
        rejectOrder();
        require(!notice.text(game, began).empty(), "Order notification was missing");
        require(!notice.text(game, began + milliseconds(3999)).empty(), "Notification disappeared too early");
        require(notice.text(game, began + seconds(4)).empty(), "Notification did not expire while simulation was paused");
        require(!game.message().empty(), "Presentation expiry changed simulation message");

        rejectOrder();
        const auto error = game.message();
        require(notice.text(game, began + seconds(5)) == error, "New notification did not appear");
        rejectOrder();
        require(game.message() == error && notice.text(game, began + seconds(8)) == error, "Repeated notification changed its text");
        require(!notice.text(game, began + milliseconds(11999)).empty(), "Repeated message failed to restart its lifetime");
        require(notice.text(game, began + seconds(12)).empty(), "Repeated notification did not expire");
        rejectOrder();
        require(notice.text(game, began + seconds(13)) == error, "Expired notification could not appear again");
        game.stop();
        require(notice.text(game, began + seconds(14)).empty(), "Cleared message remained visible");
    });
    test("Every unit uses a lower-third ground anchor for drawing and selection while moving", [&] {
        const auto defs = rts::Definitions::load(context.assets / "data/catalog.json");
        for (int level : {0, 2}) {
            rts::Scenario scene{rts::Map(40, 32), {1, 1}, {5, 9}, {}};
            for (int y = 0; y < scene.map.height(); ++y) for (int x = 0; x < scene.map.width(); ++x)
                scene.map.at({x, y}).height = level;
            int x = 5;
            for (const auto& type : defs.entities()) if (type.mobile && type.id != "human.worker") {
                scene.units.push_back({type.id, 0, {x, 15}}); x += 4;
            }
            rts::Simulation game(std::move(scene), {}, defs.entity("human.worker"), defs.entities());
            for (const auto& u : game.units())
                require(game.order(std::array{u.id}, rts::OrderKind::Move, u.cell + rts::Cell{1, 0}), "Unit failed to start moving");
            game.tick();
            for (float zoom : {.4f, 1.0f, 1.8f}) {
                const rts::WorldView view{{170, -60}, zoom};
                for (const auto& u : game.units()) {
                    const auto position = u.position;
                    const auto logical = view.project(position, game.unitHeight(u));
                    const auto feet = rts::unitScreenAnchor(view, position, game.unitHeight(u));
                    require(std::abs(feet.x - logical.x) < .001f &&
                        std::abs(feet.y - logical.y - rts::WorldView::tileSize * zoom / 3) < .001f,
                        "Unit feet are not in the middle of the lower third");
                    const auto& sprite = u.definition.sprite;
                    const auto bounds = rts::unitBounds(game, u, view);
                    require(std::abs(bounds.y - (feet.y + (.0625f - sprite.anchor.y) * sprite.size.y * zoom)) < .001f,
                        "Unit picking does not follow the drawn sprite");
                    const auto point = feet + rts::Vec2{0, -20 * zoom};
                    require(rts::pickEntity(game, view, point) == u.id, "Shifted moving sprite was not clickable");
                    rts::Selection selection;
                    selection.box(game, view, {point.x - zoom, point.y - zoom, 2 * zoom, 2 * zoom}, false);
                    require(selection.ids == std::vector{u.id}, "Small selection box missed the shifted unit");
                    require(u.position == position, "Presentation changed logical coordinates");
                }
            }
            require(game.worker().position != rts::center(game.worker().cell), "Presentation fixture did not move between cells");
        }
    });
    test("Double click selects matching friendly units in the camera and Shift adds without duplicates", [] {
        rts::EntityDefinition worker;
        auto other = worker; other.id = "other";
        rts::Scenario scene{rts::Map(40, 32), {1, 1}, {8, 8}, {}};
        scene.extraWorkers = {{10, 8}, {28, 8}, {10, 24}};
        scene.units = {{other.id, 0, {12, 8}}, {worker.id, 1, {9, 11}}, {worker.id, rts::neutralPlayer, {12, 11}}};
        rts::Simulation game(std::move(scene), {}, worker, {worker, other});
        const auto first = game.units()[0].id, second = game.units()[1].id, distant = game.units()[2].id, different = game.units()[4].id;
        for (float zoom : {.4f, 1.0f, 1.8f}) {
            const rts::WorldView view{{120, -70}, zoom};
            const auto corner = view.project({6, 6});
            const rts::UiRect viewport{corner.x, corner.y, 8 * rts::WorldView::tileSize * zoom, 6 * rts::WorldView::tileSize * zoom};
            const auto bounds = rts::unitBounds(game, game.worker(), view);
            const rts::Vec2 point{bounds.x + bounds.width * .5f, bounds.y + bounds.height * .5f};
            rts::Selection selection; selection.ids = {different};
            require(selection.selectTypeInView(game, view, viewport, point, false) && selection.ids == std::vector{first, second},
                "Double click included off-camera, foreign, neutral or differently typed units");
            const auto edge = rts::unitBounds(game, *game.unit(second), view);
            const rts::UiRect clipped{viewport.x, viewport.y, edge.x + edge.width * .5f - viewport.x, viewport.height};
            require(selection.selectTypeInView(game, view, clipped, point, false) && selection.contains(second),
                "Partially visible matching unit was excluded");
            selection.ids = {different, first};
            require(selection.selectTypeInView(game, view, viewport, point, true) && selection.ids == std::vector{different, first, second},
                "Shift double click lost selection or duplicated units");
            require(selection.selectTypeInView(game, view, viewport, point, true) && selection.ids.size() == 3,
                "Repeated Shift double click toggled or duplicated units");
            const auto selected = selection.ids;
            require(!selection.selectTypeInView(game, view, viewport, {viewport.x - 1, viewport.y - 1}, false) && selection.ids == selected,
                "A click outside the world changed selection");
            require(!selection.selectTypeInView(game, view, viewport, {viewport.x + 1, viewport.y + 1}, false) && selection.ids == selected,
                "A ground double click selected a unit type");
            auto panned = view; panned.origin.x -= 20 * rts::WorldView::tileSize * zoom;
            require(selection.selectTypeInView(game, panned, viewport, point, false) && selection.ids == std::vector{distant},
                "Type selection ignored the camera position");
        }
    });
    test("Numbered groups share members, replace independently and recall the full selection", [] {
        rts::EntityDefinition worker;
        auto soldier = worker; soldier.id = "soldier"; soldier.canBuild = false; soldier.carryCapacity = 0;
        auto scene = flatScenario();
        scene.units = {{soldier.id, 0, {5, 4}}, {soldier.id, 1, {8, 8}}};
        rts::Simulation game(std::move(scene), {}, worker, {worker, soldier});
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
        rts::Simulation game(std::move(scene), {}, worker, {worker, killer});
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
    test("Active groups start with the hero, cycle by type and preserve the full selection", [&] {
        const auto defs = rts::Definitions::load(context.assets / "data/catalog.json");
        rts::Scenario scene{rts::Map(40, 32), {3, 3}, {7, 9}, {}};
        scene.heroSpawn = rts::Cell{9, 9};
        scene.units = {{"human.archer", 0, {11, 10}}, {"human.peacemaker", 0, {10, 10}},
            {"human.peacemaker", 0, {10, 12}}, {"human.catapult", 0, {11, 11}}};
        rts::Simulation game(std::move(scene), {}, defs.entity("human.worker"), defs.entities(), "human.hero");
        rts::Selection selection;
        for (const auto& unit : game.units()) selection.ids.push_back(unit.id);
        std::reverse(selection.ids.begin(), selection.ids.end());
        const auto original = selection.ids;
        const std::array<std::string, 5> expected{"human.hero", "human.peacemaker", "human.archer", "human.catapult", "human.worker"};
        const auto groups = selection.groups(game);
        require(groups.size() == expected.size() && groups[1].ids.size() == 2, "Types were not grouped together");
        for (const auto& type : expected) {
            require(selection.activeGroup(game).type == type, "Wrong Tab order or initial hero group");
            require(selection.cycleGroup(game), "Multiple groups did not cycle");
            require(selection.ids == original, "Tab changed command recipients or selection order");
        }
        require(selection.activeUnit(game)->hero.has_value(), "Tab did not wrap to hero");
        require(selection.cycleGroup(game, true) && selection.activeGroup(game).type == "human.worker", "Reverse Tab failed");
        const auto soldier = groups[1].ids.front();
        require(selection.activateGroup(game, soldier), "Member did not activate its type");
        selection.ids.erase(std::find(selection.ids.begin(), selection.ids.end(), soldier));
        selection.prune(game);
        require(selection.activeGroup(game).type == "human.peacemaker" && selection.activeGroup(game).ids.size() == 1,
            "Removing one soldier discarded the surviving active group");
        selection.army(game);
        require(selection.activeUnit(game)->hero.has_value(), "New army selection did not reset to hero");
        selection.ids = {game.buildings().front().id}; selection.prune(game);
        require(selection.groups(game).empty() && !selection.cycleGroup(game) && !selection.activeUnit(game), "Building entered unit groups");
        selection.ids.clear(); selection.prune(game);
        require(!selection.cycleGroup(game), "Empty selection cycled");
    });
    test("Selecting a group icon isolates that exact unit and preserves numbered groups", [&] {
        const auto defs = rts::Definitions::load(context.assets / "data/catalog.json");
        rts::Scenario scene{rts::Map(24, 24), {1, 1}, {4, 3}, {}};
        scene.extraWorkers = {{5, 3}};
        scene.units = {{"human.peacemaker", 0, {6, 4}}, {"human.peacemaker", 1, {7, 4}}};
        rts::Simulation game(std::move(scene), {}, defs.entity("human.worker"), defs.entities());
        const auto first = game.worker().id, second = game.units()[1].id;
        const auto soldier = game.units()[2].id, enemy = game.units()[3].id;
        const std::vector whole{first, second, soldier};
        rts::GameplayUi ui; ui.selection.ids = whole;
        ui.controlGroups.bind(0, game, ui.selection);
        const auto saved = ui.controlGroups.members(0);
        for (const auto clicked : {second, soldier}) {
            ui.selection.ids = whole;
            ui.selection.activateGroup(game, first);
            require(ui.selection.selectMember(game, clicked) && ui.selection.ids == std::vector{clicked},
                "Icon retained other units, including members of the same type");
            require(ui.selection.activeUnit(game)->id == clicked && ui.selection.inspectedUnit(game)->id == clicked,
                "Single selection shows another unit's stats");
            require(rts::SelectionCards(game, ui.selection, rts::BattleLayout({800, 600}).info).cards.empty(),
                "Single selection retained the group grid");
            require(rts::commandRecipients(game, ui, rts::OrderKind::Move) == std::vector{clicked},
                "Commands still reach the previous group");
            require(ui.controlGroups.members(0) == saved && ui.controlGroups.recall(0, game, ui.selection) &&
                std::is_permutation(ui.selection.ids.begin(), ui.selection.ids.end(), whole.begin(), whole.end()),
                "Icon click changed the stored numbered group");
        }
        ui.selection.ids = whole;
        for (const auto invalid : {enemy, game.buildings().front().id, rts::EntityId{99999}}) {
            require(!ui.selection.selectMember(game, invalid) && ui.selection.ids == whole, "Invalid icon changed selection");
        }
    });
    test("Active type controls visible commands while general orders retain every selected unit", [&] {
        const auto defs = rts::Definitions::load(context.assets / "data/catalog.json");
        auto types = defs.entities();
        auto secondWorker = defs.entity("human.worker"); secondWorker.id = "other.worker"; types.push_back(secondWorker);
        auto secondSiege = defs.entity("human.catapult"); secondSiege.id = "other.siege"; types.push_back(secondSiege);
        rts::Scenario scene{rts::Map(40, 32), {3, 3}, {7, 9}, {}};
        scene.units = {{"human.peacemaker", 0, {10, 10}}, {"human.catapult", 0, {11, 11}},
            {secondWorker.id, 0, {8, 9}}, {secondSiege.id, 0, {12, 11}}, {"human.peacemaker", 1, {25, 25}}};
        rts::Simulation game(std::move(scene), {}, defs.entity("human.worker"), types);
        rts::GameplayUi ui;
        for (const auto& unit : game.units()) if (unit.owner == game.player().id) ui.selection.ids.push_back(unit.id);
        const auto soldier = game.units()[1].id, siege = game.units()[2].id;
        const auto slot = [](rts::UnitCommand kind) {
            for (const auto& command : rts::unitCommands) if (command.command == kind) return command.slot;
            throw std::runtime_error("Missing command");
        };
        require(ui.selection.activeGroup(game).type == "human.peacemaker", "Soldier did not lead the non-hero selection");
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
    test("A dead active group falls back safely without transferring its pending special order", [] {
        rts::EntityDefinition worker; worker.maximumHealth = 1;
        auto other = worker; other.id = "other.worker";
        auto killer = worker; killer.id = "killer"; killer.canBuild = false; killer.carryCapacity = 0;
        killer.attackDamage = 100; killer.attackWindupTicks = 1;
        rts::Scenario scene{rts::Map(40, 32), {3, 3}, {22, 20}, {}};
        scene.units = {{other.id, 0, {7, 9}}, {killer.id, 1, {23, 20}}};
        rts::Simulation game(std::move(scene), {}, worker, {worker, other, killer});
        const auto victim = game.worker().id, survivor = game.units()[1].id;
        rts::GameplayUi ui; ui.selection.ids = {victim, survivor};
        ui.commandGroup = ui.selection.activeGroup(game).type; ui.orderMode = rts::OrderKind::Gather;
        ticks(game, 20); ui.selection.prune(game);
        require(!game.unit(victim) && ui.selection.ids == std::vector{survivor}, "Dead selected unit survived pruning");
        require(ui.selection.activeUnit(game)->id == survivor, "No fallback after the active type died");
        require(rts::commandRecipients(game, ui, rts::OrderKind::Gather).empty(), "Pending gather transferred to a different worker type");
    });
    test("Large selections keep the active portraits visible within the resized command HUD", [] {
        rts::EntityDefinition worker;
        auto last = worker; last.id = "last.worker";
        rts::Scenario scene{rts::Map(64, 64), {1, 1}, {4, 3}, {}};
        for (int i = 0; i < 39; ++i) scene.extraWorkers.push_back({20 + i % 8, 20 + i / 8});
        scene.units = {{last.id, 0, {10, 10}}};
        rts::Simulation game(std::move(scene), {}, worker, {worker, last});
        rts::Selection selection;
        for (const auto& unit : game.units()) selection.ids.push_back(unit.id);
        for (rts::Vec2 extent : {rts::Vec2{800, 600}, rts::Vec2{1024, 768}, rts::Vec2{1060, 768}, rts::Vec2{1440, 900}, rts::Vec2{1920, 1080}}) {
            const rts::BattleLayout layout(extent);
            const rts::SelectionPanelLayout panel(layout.info);
            require(layout.minimap.height > layout.info.height && layout.minimap.x + layout.minimap.width < layout.info.x &&
                layout.idleWorker.y + layout.idleWorker.height < layout.minimap.y, "Larger minimap overlaps its neighbouring controls");
            require(panel.health.y >= panel.portrait.y + panel.portrait.height && panel.mana.y >= panel.health.y + panel.health.height &&
                panel.mana.x + panel.mana.width <= layout.info.x + layout.info.width &&
                panel.mana.y + panel.mana.height <= layout.info.y + layout.info.height, "Portrait values escaped their reserved rows");
            require(std::abs(layout.notification.x + layout.notification.width * .5f - extent.x * .5f) < .01f &&
                layout.notification.y + layout.notification.height < layout.controlGroups[0].y, "Notification overlaps the bottom controls");
            require(panel.content.width >= 200 && panel.portrait.x + panel.portrait.width < panel.content.x,
                "Portrait overlaps selection content or leaves too little room for stats");
            for (const auto b : layout.controlGroups)
                require(b.x >= 0 && b.x + b.width <= extent.x && b.y + b.height < extent.y - 238 &&
                    b.y + b.height < layout.commandPanel.y,
                    "Numbered group button escapes the screen or overlaps the hint strip");
            require(layout.commands.size() == 16 && layout.commands[3].y == layout.commands[0].y &&
                layout.commands[4].y > layout.commands[3].y && layout.commands[15].y > layout.commands[11].y, "Command grid is not four by four");
            require(layout.commandPanel.width == layout.minimap.width && layout.commandPanel.height == layout.minimap.height &&
                layout.commandPanel.y == layout.minimap.y &&
                std::abs(layout.commands.back().x + layout.commands.back().width - (extent.x - layout.minimap.x)) < .01f &&
                std::abs(layout.commands.back().y + layout.commands.back().height - (layout.minimap.y + layout.minimap.height)) < .01f,
                "Command block does not match the minimap size and alignment");
            const GameplayUi emptyUi;
            for (const auto button : layout.commands)
                require(!mouseInBattleWorld(emptyUi, layout, {button.x + 1, button.y + 1}), "Command slot leaked mouse input to the map");
            require(!mouseInBattleWorld(emptyUi, layout,
                {layout.commands.front().x + layout.commands.front().width + 3, layout.commands.front().y + 10}),
                "Gap between command buttons leaked mouse input to the map");
            require(layout.info.x + layout.info.width < layout.commands.front().x, "Command grid overlaps selection info");
            selection.activateGroup(game, game.units().back().id);
            const rts::SelectionCards cards(game, selection, layout.info);
            require(cards.total == 41 && (cards.cards.size() == cards.total || cards.first > 0), "Large selection did not page to its active type");
            bool visible = false;
            for (const auto& card : cards.cards) {
                visible |= card.active && card.id == game.units().back().id;
                require(card.bounds.width == (card.active ? 40 : 32), "Active portrait size did not differ");
                const auto health = card.healthBar();
                require(panel.content.contains({card.bounds.x - 1, card.bounds.y}) &&
                    panel.content.contains({health.x + health.width, health.y + health.height}) &&
                    panel.content.contains({card.manaBar().x + card.manaBar().width, card.manaBar().y + card.manaBar().height}),
                    "Card or paired bars escaped the content panel");
            }
            require(visible, "Active portrait remained off-screen");
        }
        selection.ids = {game.worker().id}; selection.prune(game);
        const rts::SelectionCards single(game, selection, rts::BattleLayout({800, 600}).info);
        require(single.total == 1 && single.cards.empty(), "Single-unit stats retained an invisible clickable card");
    });
    test("Idle worker cycling uses live free workers and skips holds, active orders and other owners", [] {
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
        require(selection.idleWorker(game, a), "Immediately stopped worker did not enter idle pool");
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
        const std::array smallSource{VisionSource{{5, 7}, 1}};
        fog.update(map, smallSource); mask.update(fog, 20, 20); raster.update(map, fog, mask, known);
        require(!fog.explored({7, 7}) && mask.lightAt(center({7, 7})) > 0 && sample({7, 7}) == 0xff081119,
            "Feathering exposed an unexplored obstacle beside the visible area");
        require(sample({13, 12}) == 0xff081119, "Black mask leaked authored impassable terrain");
    });
    test("Minimap preserves one-cell obstacles on maps larger than the raster and refreshes removed blockers", [] {
        Map map(512, 512); FogOfWar fog(512, 512); fog.revealAll(); FogMask mask; mask.update(fog, 512, 512);
        auto tree = makeEnvironment("TREE", 1000, {254, 253});
        const std::array<const EnvironmentObject*, 1> known{{&tree}};
        MinimapRaster raster; raster.update(map, fog, mask);
        const auto ground = raster.pixels();
        require(raster.update(map, fog, mask, known) && raster.pixels() != ground,
            "A point obstacle fell between minimap samples");
        require(!raster.update(map, fog, mask, known), "Unchanged blockers regenerated the raster");
        require(raster.update(map, fog, mask) && raster.pixels() == ground, "Removed known obstacle remained cached");
    });
    test("Minimap marks cliff edges using terrain traversal and leaves wide ramps open", [] {
        Map map(16, 16);
        for (int y = 0; y < 16; ++y) for (int x = 8; x < 16; ++x) map.at({x, y}).height = 1;
        for (int y = 6; y <= 8; ++y) map.at({7, y}).ramp = {1, 0};
        FogOfWar fog(16, 16); fog.revealAll(); FogMask mask; mask.update(fog, 16, 16);
        MinimapRaster raster; raster.update(map, fog, mask);
        const MinimapProjection projection({0, 0, 164, 164}, map);
        const auto sample = [&](Vec2 world) {
            const auto p = projection.project(world);
            return raster.pixels()[size_t(p.y) * 164 + size_t(p.x)];
        };
        require(sample({7.98f, 4.5f}) != sample({7.5f, 4.5f}), "Cliff boundary is missing");
        require(sample({7.98f, 7.5f}) == sample({7.5f, 7.5f}), "Ramp crossing was drawn as a cliff");
        map.at({7, 7}).ramp = {};
        require(raster.update(map, fog, mask) && sample({7.98f, 7.5f}) != sample({7.5f, 7.5f}),
            "Ramp edit left a stale path through the cliff");
    });
    test("Building sprite selection, box groups, additive selection and camera transforms", [] {
        auto s = flatScenario(); s.extraWorkers = {{5, 3}, {4, 4}};
        rts::Simulation game(std::move(s));
        rts::WorldView view{{500, 40}, 1.25f};
        rts::Selection selection;
        const auto bounds = rts::buildingBounds(game, game.buildings()[0], view);
        selection.click(game, view, {bounds.x + bounds.width * .5f, bounds.y + 30}, false);
        require(selection.ids.size() == 1 && selection.ids[0] == game.buildings()[0].id, "Building upper sprite not selectable");
        const auto p = rts::unitScreenAnchor(view, game.worker().position, game.workerHeight()) + rts::Vec2{0, -25 * view.zoom};
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
        auto s = flatScenario(rts::Crystal::defaultReserve); s.extraWorkers = {{5, 3}};
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
            const auto workerPoint = rts::unitScreenAnchor(view, game.worker().position, game.workerHeight()) + rts::Vec2{0, -25 * zoom};
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
        const auto back = rts::unitScreenAnchor(view, game.units()[0].position, 0) + rts::Vec2{0, -25};
        require(rts::pickEntity(game, view, back) == game.units()[0].id, "Unit behind the building cannot be selected through its window");
        const auto front = rts::unitScreenAnchor(view, game.units()[1].position, 0) + rts::Vec2{0, -48};
        require(rts::pickEntity(game, view, front) == game.units()[1].id, "Foreground unit hidden from selection");
    });
    test("Occlusion windows share picking, camera scale and logical visibility", [] {
        Scenario s{Map(24, 24), {1, 1}, {3, 0}, {}};
        Simulation game(std::move(s));
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
