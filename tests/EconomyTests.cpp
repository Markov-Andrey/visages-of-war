#include "TestSupport.hpp"

namespace rts::tests {
void economyTests(TestSuite& test, const TestContext& context) {
    const auto& assets=context.assets;
    test("Construction rejects a neighbouring circle crossing its square footprint", [] {
        rts::Scenario s{rts::Map(20, 16), {1, 1}, {6, 6}, {}};
        rts::Simulation game(std::move(s));
        game.command({9, 6});
        for (int tick = 0; tick < 60 && game.worker().position.x < 7.75f; ++tick) game.tick();
        game.stop();
        require(game.worker().cell == rts::Cell{7, 6}, "Test worker entered the construction cell");
        require(!game.canPlace("human.hall", {8, 6}), "Building overlapped the edge of a neighbouring circle");
        require(game.canPlace("human.hall", {9, 6}), "Non-overlapping circle blocked construction");
    });
    test("Crystals are credited only at hall; last partial load is returned", [] {
        rts::Simulation game(flatScenario());
        game.command({7, 7});
        for (int i = 0; i < 1000 && game.worker().cargo == 0; ++i) game.tick();
        require(game.worker().cargo > 0 && game.storedCrystals() == 0, "Credited before deposit");
        ticks(game, 6000);
        require(game.storedCrystals() == 23 && game.worker().cargo == 0, "Lost partial load or duplicated resource");
        require(game.crystals()[0].remaining == 0 && game.map().walkable({7, 7}), "Exhausted node still blocking");
        require(game.worker().state == rts::UnitState::Idle, "Worker did not finish");
    });
    test("Large crystals accept clicks on every cell and release the entire footprint after harvesting", [&] {
        const auto& world = context.worldAssets;
        for (const auto* kind : {"crystal.medium", "crystal.big"}) {
            Scenario s{Map(24, 24), {1, 1}, {17, 14}, {world.instantiateCrystal(kind, {13, 13})}};
            s.crystals.front().remaining = 2;
            const auto node = s.crystals.front();
            rebuildScenario(s, {3, 2});
            auto worker = gatheringWorker(); worker.dayVision = worker.nightVision = 3;
            Simulation game(std::move(s), {}, worker);
            const auto id = game.crystals().front().id;
            require(!game.fog().visible(node.cell) && game.crystalVisible(game.crystals().front()), "Partial footprint visibility lost");
            for (int y = 0; y < node.height; ++y) for (int x = 0; x < node.width; ++x)
                require(game.command(node.cell + Cell{x, y}), "A crystal footprint cell rejected gathering");
            const auto slots = perimeter(game.map(), node.cell, node.width, node.height);
            require(std::find(slots.begin(), slots.end(), game.worker().route.back()) != slots.end(), "Worker did not approach the outer perimeter");
            ticks(game, 1800);
            require(game.crystals().front().remaining == 0 && game.storedCrystals() == 2 && game.crystals().front().id == id, "Depletion lost cargo or resource identity");
            for (int y = 0; y < node.height; ++y) for (int x = 0; x < node.width; ++x)
                require(game.map().walkable(node.cell + Cell{x, y}), "Depleted footprint still blocks movement");
        }
    });
    test("Explicit gathering crosses temporary traffic instead of switching to a nearer field", [] {
        auto s = gatheringScenario();
        for (int y = 0; y < 16; ++y) if (y != 8) s.map.at({8, y}).blocked = true;
        s.extraWorkers = {{8, 8}};
        rts::Simulation game(std::move(s), {}, gatheringWorker());
        const auto blocker = game.units()[1].id;
        require(game.command({15, 8}), "Gather command rejected");
        require(game.worker().targetCrystal == 0, "Temporary traffic redirected gathering to another field");
        ticks(game, 210);
        require(game.worker().targetCrystal == 0 && game.crystals()[1].remaining == 1000, "Waiting worker abandoned clicked deposit");
        game.command(std::span<const rts::EntityId>(&blocker, 1), {10, 11});
        ticks(game, 1600);
        require(game.crystals()[0].remaining < 1000 && game.storedCrystals() > 0, "Gathering did not resume after traffic cleared");
        require(game.crystals()[1].remaining == 1000 && game.crystals()[2].remaining == 1000, "Worker changed deposit after delivery");
    });
    test("Full gathering slots select a neighbor of the clicked node and retain that area after delivery", [] {
        auto s = gatheringScenario();
        s.extraWorkers = {{14, 8}, {16, 8}, {15, 7}, {15, 9}};
        rts::Simulation game(std::move(s), {}, gatheringWorker());
        game.command({15, 8});
        require(game.worker().targetCrystal == 2, "Chose worker-nearest crystal instead of target-nearest neighbor");
        ticks(game, 2200);
        require(game.storedCrystals() >= 20 && game.crystals()[2].remaining < 1000, "Neighbor gathering failed to repeat after depositing");
        require(game.crystals()[1].remaining == 1000, "Delivery lost the original gathering area");
    });
    test("Saturated resource field waits for a slot without selecting a remote crystal", [] {
        auto s = gatheringScenario(false);
        s.extraWorkers = {{14, 8}, {16, 8}, {15, 7}, {15, 9}};
        rts::Simulation game(std::move(s), {}, gatheringWorker());
        const auto blocker = game.units()[1].id;
        game.command({15, 8}); ticks(game, 450);
        require(game.worker().targetCrystal == 0 && game.worker().repeatGather, "Full deposit lost its gather order");
        require(game.crystals()[1].remaining == 1000, "Saturation sent worker to a remote deposit");
        game.command(std::span<const rts::EntityId>(&blocker, 1), {12, 6});
        ticks(game, 1500);
        require(game.crystals()[0].remaining < 1000 && game.storedCrystals() > 0, "Freed gather slot was not used");
    });
    test("Depletion stays near original gather target and never chains into another field", [] {
        auto s = gatheringScenario();
        s.crystals[0].remaining = 3; s.crystals[2].remaining = 4;
        s.crystals.push_back({{20, 8}, 1000}); s.map.occupy({20, 8});
        rts::Simulation game(std::move(s), {}, gatheringWorker());
        game.command({15, 8}); ticks(game, 5000);
        require(game.storedCrystals() == 7 && game.worker().cargo == 0, "Did not finish original local deposits exactly");
        require(game.crystals()[1].remaining == 1000 && game.crystals()[3].remaining == 1000, "Gathering drifted beyond the original area");
        require(game.worker().state == rts::UnitState::Idle, "Exhausted area never finished");
    });
    test("Blocked gathering retains intent until terrain opens and Stop cancels retry", [] {
        auto s = gatheringScenario(false);
        // An observer beyond the barrier keeps the requested deposit visible;
        // this test isolates blocked movement from the new forest sight rule.
        s.extraWorkers.push_back({14, 6});
        for (int y = 0; y < 16; ++y) if (y != 8) s.map.at({8, y}).blocked = true;
        auto tree = rts::makeEnvironment("TREE", 777, {8, 8});
        s.environment.push_back(tree); s.map.occupy(tree.origin);
        rts::Simulation game(std::move(s), {}, gatheringWorker());
        require(game.fog().visible({15, 8}), "Observer did not reveal the gathering target");
        game.command({15, 8}); ticks(game, 180);
        require(game.worker().repeatGather && game.crystals()[1].remaining == 1000, "Static obstruction redirected gather order");
        game.damageEnvironment(tree.id, tree.maximumHitPoints); ticks(game, 1500);
        require(game.crystals()[0].remaining < 1000 && game.crystals()[1].remaining == 1000, "Gather did not resume when terrain opened");
        game.stop(); ticks(game, 30);
        const int remaining = game.crystals()[0].remaining;
        ticks(game, 180);
        require(game.worker().state == rts::UnitState::Idle && !game.worker().repeatGather && game.crystals()[0].remaining == remaining, "Stop did not cancel gather retry");
    });
    test("Group gathering reserves local slots and conserves resources without changing fields", [] {
        auto s = gatheringScenario();
        s.extraWorkers = {{4, 7}, {4, 9}, {3, 8}, {3, 7}, {3, 9}};
        rts::Simulation game(std::move(s), {}, gatheringWorker());
        std::vector<rts::EntityId> ids;
        for (const auto& u : game.units()) ids.push_back(u.id);
        require(game.command(ids, {15, 8}), "Group gather rejected");
        for (const auto& u : game.units()) require(u.targetCrystal == 0 || u.targetCrystal == 2, "Group chose another resource field");
        for (int tick = 0; tick < 2500; ++tick) {
            game.tick();
            int total = game.storedCrystals();
            for (const auto& c : game.crystals()) total += c.remaining;
            for (const auto& u : game.units()) {
                total += u.cargo;
                require(u.gatherOriginCrystal == 0 && u.targetCrystal != 1, "Group lost requested deposit after retry/delivery");
            }
            require(total == 3000, "Group gathering lost or duplicated crystals");
        }
        require(game.storedCrystals() > 0 && game.crystals()[1].remaining == 1000, "Group failed to deliver from selected field");
    });
    test("Gather ordered mid-step retains the clicked area when the crystal depletes", [] {
        auto s = gatheringScenario();
        s.worker = {14, 10}; s.extraWorkers = {{15, 7}}; s.crystals[0].remaining = 1;
        auto type = gatheringWorker(); type.movementPerSecond = .5f;
        rts::Simulation game(std::move(s), {}, type);
        const auto other = game.units()[1].id;
        game.command(std::span<const rts::EntityId>(&other, 1), {15, 8});
        game.command({14, 9}); ticks(game, 1);
        require(!game.worker().route.empty() && game.worker().position != rts::center(game.worker().cell), "Test did not begin a movement step");
        game.command({15, 8});
        require(game.worker().gatherOriginCrystal == 0, "Gather did not retain the clicked origin");
        ticks(game, 350);
        require(game.crystals()[0].remaining == 0, "Other worker did not deplete clicked deposit");
        require(game.worker().gatherOriginCrystal == 0 && game.worker().targetCrystal == 2, "Replacement gather lost depleted target's area");
        require(game.crystals()[1].remaining == 1000 && game.crystals()[2].remaining < 1000, "Replacement gather selected wrong field");
    });
    test("Unreachable deposit preserves carried resources", [] {
        auto scenario = flatScenario(4); scenario.worker = {5, 3};
        for (int y = 0; y < 10; ++y) scenario.map.at({4, y}).blocked = true;
        rts::Simulation game(std::move(scenario));
        game.command({7, 7});
        ticks(game, 1200);
        require(game.storedCrystals() == 0 && game.worker().cargo == 4, "Cargo lost on failed return path");
        require(game.worker().state == rts::UnitState::Idle, "Unreachable hall retry loop");
    });
    test("Construction validates site, charges once, resumes and releases only its footprint", [&] {
        auto definitions = rts::Definitions::load(assets / "data/catalog.json");

        auto s = flatScenario(); s.startingCrystals = 500;
        rts::Simulation game(std::move(s), {}, definitions.entity("human.worker"), definitions.entities());
        const std::array<rts::EntityId, 1> ids{game.worker().id};
        require(!game.construct(ids, "human.barracks", {4, 3}), "Built on worker");
        require(!game.construct(ids, "human.barracks", {8, 8}), "Built outside map");
        require(!game.construct(ids, "human.barracks", {6, 6}), "Built over crystal");
        require(game.storedCrystals() == 500, "Invalid placement consumed crystals");
        const auto building = game.construct(ids, "human.barracks", {5, 3});
        require(building && !game.map().walkable({5, 3}) && game.storedCrystals() == 400, "Foundation/cost wrong");
        require(!game.train(*building), "Unfinished building produced unit");
        ticks(game, 30); game.stop(ids);
        const int progress = game.building(*building)->constructionProgress;
        ticks(game, 60);
        require(progress > 0 && game.building(*building)->constructionProgress == progress, "Construction advanced without builder");
        game.command(ids, {5, 3}); ticks(game, 300);
        require(game.building(*building)->complete(), "Resumed construction never finished");
        require(game.building(*building)->health == 700, "Completed building health wrong");
        const auto tower = game.construct(ids, "human.watchtower", {3, 5});
        require(tower && game.storedCrystals() == 325, "Tower placement failed");
        require(game.cancelConstruction(*tower), "Cancellation failed");
        require(game.map().walkable({3, 5}) && game.storedCrystals() == 400, "Cancellation lost resource or occupancy");
        require(!game.map().walkable({5, 3}) && !game.cancelConstruction(*building), "Cancellation touched completed building");
    });
    test("Common building costs reserve supply atomically and cancellation refunds both resources", [&] {
        const auto definitions = rts::Definitions::load(assets / "data/catalog.json");
        auto types = definitions.entities();
        for (auto& type : types) if (type.id == "human.watchtower") type.cost.supply = 99;
        auto s = flatScenario(); s.startingCrystals = 500;
        rts::Simulation game(std::move(s), {}, definitions.entity("human.worker"), std::move(types));
        const std::array<rts::EntityId, 1> ids{game.worker().id};
        const auto tower = game.construct(ids, "human.watchtower", {3, 5});
        require(tower && game.armySupply().used() == 100 && game.storedCrystals() == 425, "Building did not reserve its common cost");
        require(!game.construct(ids, "human.watchtower", {4, 5}), "Construction exceeded supply cap");
        require(game.map().walkable({4, 5}) && game.storedCrystals() == 425 && game.buildings().size() == 2, "Rejected construction changed world or resources");
        require(game.cancelConstruction(*tower), "Could not cancel supply-consuming construction");
        require(game.map().walkable({3, 5}) && game.armySupply().used() == 1 && game.storedCrystals() == 500, "Cancellation did not restore footprint and both costs");
        require(!game.cancelConstruction(*tower) && game.storedCrystals() == 500, "Cancellation refunded twice");
    });
    test("Initial production exits below the left corner for every footprint and rally destination", [] {
        for (const rts::Cell size : {rts::Cell{3, 2}, rts::Cell{1, 1}, rts::Cell{2, 4}}) for (const bool moveRally : {false, true}) {
            rts::Scenario s{rts::Map(16, 12), {2, 2}, {10, 8}, {}}; s.startingCrystals = 100;
            rts::EntityDefinition worker; worker.trainingTicks = 1;
            auto depot = testDepot("test.depot", worker.id); depot.width = size.x; depot.height = size.y;
            rts::Simulation game(std::move(s), {}, worker, {depot});
            const auto hall = game.buildings().front().id;
            const rts::Cell exit{2, 2 + size.y};
            require(game.building(hall)->rally == exit, "Initial rally is not below the leftmost footprint cell");
            const rts::Cell destination = moveRally ? rts::Cell{11, 8} : exit;
            if (moveRally) require(game.setRally(hall, destination), "Could not move rally");
            require(game.train(hall), "Could not train at initial depot");
            game.tick();
            require(game.units().size() == 2 && game.units().back().cell == exit, "Rally or footprint changed the preferred exit");
            ticks(game, 300);
            require(game.units().back().cell == destination && game.units().back().state == rts::UnitState::Idle, "Produced unit missed its rally");
        }
    });
    test("Constructed producers share the initial depot's exit and default rally rule", [] {
        auto s = flatScenario(); s.startingCrystals = 300;
        rts::EntityDefinition worker; worker.trainingTicks = 1;
        auto producer = testDepot("test.producer", worker.id);
        producer.acceptsCargo = false; producer.width = 2; producer.height = 3;
        rts::Simulation game(std::move(s), {}, worker, {producer});
        const std::array<rts::EntityId, 1> builders{game.worker().id};
        const auto id = game.construct(builders, producer.id, {5, 3});
        require(id.has_value(), "Could not construct producer");
        require(game.building(*id)->rally == rts::Cell{5, 6}, "New building rally ignored its footprint");
        ticks(game, 30);
        require(game.building(*id)->complete() && game.train(*id), "New producer did not complete or train");
        game.tick();
        require(game.units().size() == 2 && game.units().back().cell == rts::Cell{5, 6}, "New producer used a different exit rule");
    });
    test("Unavailable lower-left exits retain a valid fallback for rally and production", [] {
        for (const bool mapEdge : {false, true}) {
            auto s = flatScenario(); s.startingCrystals = 100;
            if (mapEdge) s.hall = {1, 8};
            else s.map.at({1, 3}).blocked = true;
            rts::EntityDefinition worker; worker.trainingTicks = 1;
            rts::Simulation game(std::move(s), {}, worker);
            const auto& hall = game.buildings().front();
            const rts::Cell expected = mapEdge ? rts::Cell{4, 9} : rts::Cell{2, 3};
            require(hall.rally == expected && game.map().walkable(hall.rally), "Default rally selected an unavailable exit");
            require(game.train(hall.id), "Could not train with blocked preferred exit");
            game.tick();
            require(game.units().size() == 2 && game.units().back().cell == expected, "Production did not use a valid fallback");
        }
    });
    test("Successive production fills the perimeter counterclockwise and waits until a slot opens", [] {
        struct Case { rts::Cell size; std::vector<rts::Cell> exits; };
        const std::array<Case, 3> cases{{
            {{3, 2}, {{2, 4}, {3, 4}, {4, 4}, {5, 3}, {5, 2}, {4, 1}, {3, 1}, {2, 1}, {1, 2}, {1, 3}}},
            {{1, 1}, {{2, 3}, {3, 2}, {2, 1}, {1, 2}}},
            {{2, 3}, {{2, 5}, {3, 5}, {4, 4}, {4, 3}, {4, 2}, {3, 1}, {2, 1}, {1, 2}, {1, 3}, {1, 4}}}
        }};
        for (const auto& setup : cases) {
            rts::Scenario s{rts::Map(16, 12), {2, 2}, {10, 8}, {}}; s.startingCrystals = 1000;
            rts::EntityDefinition worker; worker.trainingTicks = 1;
            auto depot = testDepot("test.depot", worker.id); depot.width = setup.size.x; depot.height = setup.size.y;
            rts::Simulation game(std::move(s), {}, worker, {depot});
            const auto hall = game.buildings().front().id;
            rts::EntityId first{};
            for (const auto exit : setup.exits) {
                const auto count = game.units().size();
                require(game.train(hall), "Could not train next unit");
                game.tick();
                require(game.units().size() == count + 1 && game.units().back().cell == exit, "Successive exit skipped counterclockwise order");
                const auto id = game.units().back().id;
                if (!first) first = id;
                game.stop(std::array<rts::EntityId, 1>{id});
            }
            const auto count = game.units().size();
            require(game.train(hall), "Could not queue blocked production");
            const auto supply = game.armySupply().used();
            ticks(game, 10);
            require(game.units().size() == count && game.building(hall)->production.size() == 1 &&
                game.building(hall)->production.front().remainingTicks == 0, "Full perimeter did not hold the completed job");
            require(game.armySupply().used() == supply, "Blocked production lost reserved supply");
            for (size_t a = 0; a < count; ++a) for (size_t b = a + 1; b < count; ++b)
                require(separated(game.units()[a], game.units()[b]), "Production overlapped units");
            require(game.command(std::array<rts::EntityId, 1>{first}, {2, 7}), "Could not clear the first exit");
            ticks(game, 60);
            require(game.units().size() == count + 1 && game.building(hall)->production.empty(), "Production did not resume after clearing an exit");
            require(game.units().back().cell == setup.exits.front() && game.armySupply().used() == supply, "Resumed production used the wrong exit or charged supply twice");
        }
    });
    test("Production skips cliffs, ramps, obstacles and incompatible water in perimeter order", [] {
        auto s = flatScenario(); s.startingCrystals = 100;
        s.map.at({1, 3}).height = -1;
        s.map.at({2, 3}).ramp = {0, 1};
        s.map.at({3, 3}).height = 1;
        s.map.at({4, 2}).blocked = true;
        s.map.at({4, 1}).surface = rts::Surface::DeepWater;
        rts::EntityDefinition worker; worker.trainingTicks = 1;
        rts::Simulation game(std::move(s), {}, worker);
        const auto hall = game.buildings().front().id;
        require(game.building(hall)->rally == rts::Cell{3, 0}, "Default rally selected forbidden terrain or skipped perimeter order");
        require(game.train(hall), "Could not train near terrain obstacles");
        game.tick();
        require(game.units().size() == 2 && game.units().back().cell == rts::Cell{3, 0}, "Production selected forbidden terrain or skipped perimeter order");
    });
    test("Production skips an exit intersecting a moving circle", [] {
        auto s = flatScenario(); s.startingCrystals = 100; s.worker = {1, 4};
        rts::EntityDefinition worker; worker.trainingTicks = 1;
        rts::Simulation game(std::move(s), {}, worker);
        require(game.command({1, 3}), "Could not move worker into preferred exit");
        ticks(game, 4);
        require(!game.worker().route.empty() && game.worker().position != rts::center(game.worker().cell) && game.worker().cell == rts::Cell{1, 4}, "Worker did not approach the exit");
        require(game.train(game.buildings().front().id), "Could not train near moving worker");
        game.tick();
        require(game.units().size() == 2 && game.units().back().cell == rts::Cell{2, 3}, "Production ignored circular clearance or skipped lower edge");
    });
    test("Production reserves supply, refunds cancellation and follows per-building rally", [&] {
        auto definitions = rts::Definitions::load(assets / "data/catalog.json");

        auto s = flatScenario(); s.startingCrystals = 500;
        rts::Simulation game(std::move(s), {}, definitions.entity("human.worker"), definitions.entities());
        const auto hall = game.buildings()[0].id;
        const std::array<rts::EntityId, 1> ids{game.worker().id};
        const auto barracks = game.construct(ids, "human.barracks", {5, 3});
        require(barracks.has_value(), "No barracks"); ticks(game, 300);
        require(game.setRally(hall, {3, 7}) && game.setRally(*barracks, {8, 5}), "Rally rejected");
        require(game.building(hall)->rally != game.building(*barracks)->rally, "Rally not independent");
        require(game.train(*barracks) && game.train(*barracks), "Cannot queue two soldiers");
        require(game.building(*barracks)->training(), "Queued production did not activate training effects");
        require(game.armySupply().used() == 5 && game.storedCrystals() == 280, "Queue did not reserve cost and supply");
        require(game.cancelTraining(*barracks), "Queue cancellation failed");
        require(game.building(*barracks)->training(), "Cancelling one of two jobs stopped training effects");
        require(game.armySupply().used() == 3 && game.storedCrystals() == 340, "Queue refund incorrect");
        ticks(game, 600);
        require(game.units().size() == 2 && game.armySupply().used() == 3, "Spawn charged supply twice");
        require(!game.building(*barracks)->training(), "Finished production left training effects active");
        require(game.train(*barracks) && game.cancelTraining(*barracks) && !game.building(*barracks)->training(), "Cancelled last job left training effects active");
        require(game.units().back().definitionId == "human.peacemaker" && game.units().back().cell == rts::Cell{8, 5}, "Produced unit missed rally");
        require(!game.setRally(*barracks, {7, 7}), "Rally accepted blocked crystal");
    });
    test("Completed production waits for a blocked exit and can still be cancelled", [] {
        auto s = flatScenario(); s.startingCrystals = 300; s.worker = {1, 0};
        for (rts::Cell c : rts::perimeter(s.map, s.hall, 3, 2)) if (c != s.worker) s.map.at(c).blocked = true;
        rts::Simulation game(std::move(s));
        const auto hall = game.buildings()[0].id;
        require(game.train(hall), "Could not train");
        ticks(game, 150);
        require(game.units().size() == 1 && game.building(hall)->production.front().remainingTicks == 0, "Spawn overlapped blocked exit");
        require(!game.building(hall)->training(), "Finished job blocked at exit still counted as training");
        require(game.cancelTraining(hall) && game.armySupply().used() == 1 && game.storedCrystals() == 300, "Blocked job not refundable");
        require(game.train(hall), "Could not retrain");
        game.command({0, 0}); ticks(game, 150);
        require(game.units().size() == 2 && game.armySupply().used() == 2, "Production did not resume after exit cleared");
    });
    test("Queue limit and hard supply cap reject without consuming crystals", [] {
        auto s = flatScenario(); s.startingCrystals = 1000;
        rts::EntityDefinition type; type.cost.supply = 20;
        rts::Simulation game(std::move(s), {}, type);
        const auto hall = game.buildings()[0].id;
        for (int i = 0; i < 4; ++i) require(game.train(hall), "Could not reserve full army");
        const int balance = game.storedCrystals();
        require(!game.train(hall) && game.armySupply().used() == 100 && game.storedCrystals() == balance, "Army exceeded 100 or failed order charged");
        require(game.cancelTraining(hall) && game.train(hall), "Released supply not reusable");
        auto normal = flatScenario(); normal.startingCrystals = 1000;
        rts::Simulation queued(std::move(normal));
        const auto depot = queued.buildings()[0].id;
        for (int i = 0; i < 5; ++i) require(queued.train(depot), "Queue rejected available slot");
        require(!queued.train(depot) && queued.building(depot)->production.size() == 5, "Queue exceeded five slots");
    });
    test("Several workers conserve resources while sharing gathering and depot approaches", [] {
        auto s = flatScenario(73); s.extraWorkers = {{5, 3}, {4, 4}, {5, 4}};
        rts::Simulation game(std::move(s));
        std::vector<rts::EntityId> ids;
        for (const auto& u : game.units()) ids.push_back(u.id);
        game.command(ids, {7, 7});
        for (int tick = 0; tick < 7000; ++tick) {
            game.tick();
            int total = game.storedCrystals() + game.crystals()[0].remaining;
            for (const auto& u : game.units()) total += u.cargo;
            require(total == 73, "Resources lost or duplicated");
        }
        require(game.storedCrystals() == 73, "Workers failed to deliver shared deposit");
    });
}
}
