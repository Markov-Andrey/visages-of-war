#include "TestSupport.hpp"

namespace rts::tests {
void movementTests(TestSuite& test, const TestContext& context) {
    test("Equal visible distances take equal time in all eight directions", [] {
        // These integer grid displacements describe a circle on the flat screen:
        // radius 10, including 6:8 directions. Measure actual elapsed
        // simulation ticks and per-tick pixels, independently of the metric helper.
        for (auto movement : {MovementType::Walking, MovementType::Flying}) {
            for (Cell d : {Cell{5,-5}, {-5,5}, {10,10}, {-10,-10}, {10,2}, {-10,-2}, {2,10}, {-2,-10}}) {
                Scenario s{Map(64,64), {1,1}, {30,30}, {}};
                EntityDefinition type; type.movement = movement; type.movementPerSecond = 3;
                auto game = makeTestSimulation(std::move(s), {}, type);
                const auto id = game.worker().id;
                const Vec2 start = game.worker().position;
                const Cell goal = game.worker().cell + d;
                require(game.order(std::array{id}, OrderKind::Move, goal), "Direction order failed");
                const WorldView view{{143,-82}, 1.7f};
                const auto delta = view.project(center(goal)) - view.project(start);
                const float pixels = std::hypot(delta.x, delta.y);
                const float step = type.movementPerSecond * WorldView::tileSize / std::sqrt(1.25f) * view.zoom / Simulation::ticksPerSecond;
                const int expected = int(std::ceil(pixels / step - .0001f));
                int elapsed = 0;
                while (game.worker().state != UnitState::Idle && elapsed < 300) {
                    const auto before = view.project(game.worker().position);
                    game.tick(); ++elapsed;
                    const auto travel = view.project(game.worker().position) - before;
                    const float actual = std::hypot(travel.x, travel.y);
                    require(actual <= step + .002f, "Direction exceeded visible speed");
                    if (elapsed < expected) require(std::abs(actual - step) < .002f, "Visible speed depends on direction");
                }
                require(std::abs(elapsed - expected) <= 1 && game.worker().cell == goal, "Travel time depends on direction");
            }
        }
    });
    const auto loadScenario = [&](const std::filesystem::path& file) { return rts::loadScenario(file, context.worldAssets, context.hallFootprint); };
    test("Circle sweeps preserve walls, corners, shore ramps and the air layer", [] {
        rts::Map map(12, 12);
        map.occupy({5, 5});
        require(!map.canTraverse({2.5f, 5.5f}, {9.5f, 5.5f}, .2f), "Sweep tunnelled through a building");
        require(map.canTraverse({2.5f, 4.75f}, {9.5f, 4.75f}, .2f), "Small circle could not pass beside building");
        require(!map.canTraverse({2.5f, 4.75f}, {9.5f, 4.75f}, .3f), "Radius ignored beside building");
        require(map.canTraverse({2.5f, 5.5f}, {9.5f, 5.5f}, .5f, rts::MovementType::Flying), "Building blocked air");
        require(!map.canTraverse({.5f, .5f}, {.1f, .5f}, .2f, rts::MovementType::Flying), "Flying circle escaped map");
        require(!map.canTraverse({4.5f, 5.5f}, {5.5f, 4.5f}, .2f), "Circle cut a blocked corner");
        auto shore = shoreScenario();
        require(shore.map.canTraverse({10.5f, 10.5f}, {13.5f, 10.5f}, .5f), "Circle cannot use shore ramp");
        require(!shore.map.canTraverse({10.5f, 5.5f}, {13.5f, 5.5f}, .2f), "Circle bypassed shore cliff");
        require(!shore.map.canTraverse({10.5f, 10.5f}, {11.5f, 11.5f}, .2f), "Smoothing cut ramp corner");
    });
    test("Different circle sizes remain separated through crossing and replacement orders", [] {
        rts::Scenario s{rts::Map(24, 20), {1, 1}, {5, 8}, {}};
        rts::EntityDefinition large; large.id = "test.large"; large.collisionRadius = .5f; large.movementPerSecond = 15;
        s.units = {{large.id, 0, {12, 8}}};
        auto game = makeTestSimulation(std::move(s), {}, {}, {large});
        const auto a = game.units()[0].id, b = game.units()[1].id;
        game.order(std::span<const rts::EntityId>(&a, 1), rts::OrderKind::Move, {18, 8});
        game.order(std::span<const rts::EntityId>(&b, 1), rts::OrderKind::Move, {4, 8});
        for (int tick = 0; tick < 600; ++tick) {
            if (tick == 20) game.stop(std::span<const rts::EntityId>(&a, 1));
            if (tick == 25) game.order(std::span<const rts::EntityId>(&a, 1), rts::OrderKind::Move, {18, 8});
            const auto beforeA = game.unit(a)->position, beforeB = game.unit(b)->position;
            game.tick();
            require(separated(game.units()[0], game.units()[1]), "Different-size circles intersected");
            for (float t : {.25f, .5f, .75f}) {
                const auto pa = beforeA + (game.unit(a)->position - beforeA) * t;
                const auto pb = beforeB + (game.unit(b)->position - beforeB) * t;
                require(groundLength(pa - pb) + .00001f >=
                    game.unit(a)->definition.collisionRadius + game.unit(b)->definition.collisionRadius,
                    "Moving circles intersected between simulation ticks");
            }
        }
        require(game.unit(a)->cell == rts::Cell{18, 8} && game.unit(b)->cell == rts::Cell{4, 8}, "Crossing traffic stalled");
    });
    test("Mid-step replacement orders and stop do not teleport", [] {
        auto game = makeTestSimulation(flatScenario());
        game.command({8, 3});
        ticks(game, 3);
        auto previous = game.worker().position;
        game.command({4, 8});
        for (int i = 0; i < 100; ++i) {
            game.tick();
            const auto now = game.worker().position;
            require(groundLength(now - previous) < 0.1f, "Teleport after replacement order");
            previous = now;
        }
        game.stop();
        ticks(game, 20);
        require(game.worker().state == rts::UnitState::Idle, "Stop ignored");
    });
    test("Lowered water requires shore ramps and preserves movement types", [] {
        using M = rts::MovementType;
        auto s = shoreScenario();
        const auto& map = s.map;
        for (const auto movement : {M::Walking, M::Amphibious}) {
            const auto path = rts::findPath(map, {4, 10}, {27, 10}, movement);
            require(path.has_value(), "Cannot cross lowered ford using shore ramps");
            require(std::find(path->cells.begin(), path->cells.end(), rts::Cell{11, 10}) != path->cells.end() &&
                std::find(path->cells.begin(), path->cells.end(), rts::Cell{20, 10}) != path->cells.end(), "Crossing bypassed shore ramps");
            require(!map.canStep({11, 5}, {12, 5}, movement) && !map.canStep({12, 5}, {11, 5}, movement), "Unit jumped between land 0 and water -1");
            require(!map.canStep({10, 10}, {11, 11}, movement), "Diagonal cut into shore ramp");
        }
        require(map.canStep({13, 5}, {14, 5}, M::Swimming) && map.canStep({13, 5}, {14, 5}, M::Amphibious), "Shallow/deep boundary became a cliff");
        require(!map.canStep({13, 5}, {14, 5}, M::Walking), "Walking unit entered deep water");
        require(!map.canStep({12, 10}, {11, 10}, M::Swimming), "Ship climbed a dry bank ramp");
        require(map.canStep({11, 5}, {12, 5}, M::Flying), "Flyer blocked by shore cliff");
        for (int y : {10, 11}) { s.map.at({11, y}).ramp = {}; s.map.at({20, y}).ramp = {}; }
        require(!rts::findPath(s.map, {4, 10}, {27, 10}, M::Walking) &&
            !rts::findPath(s.map, {4, 10}, {27, 10}, M::Amphibious), "Non-flying unit crossed without ramps");
    });
    test("Water movement matrix, corners, cliffs and independent air layer", [] {
        using M = rts::MovementType; using S = rts::Surface;
        rts::Map map(9, 7);
        for (int y = 0; y < 7; ++y) { map.at({3, y}).surface = S::ShallowWater; map.at({4, y}).surface = S::DeepWater; map.at({5, y}).surface = S::ShallowWater; }
        require(map.walkable({3, 2}, M::Walking) && !map.walkable({4, 2}, M::Walking), "Walker water rules");
        require(!map.walkable({2, 2}, M::Swimming) && map.walkable({3, 2}, M::Swimming) && map.walkable({4, 2}, M::Swimming), "Swimmer water rules");
        require(!rts::findPath(map, {1, 2}, {7, 2}, M::Walking), "Walker crossed deep channel");
        require(rts::findPath(map, {1, 2}, {7, 2}, M::Amphibious).has_value(), "Amphibian cannot cross water");
        require(rts::findPath(map, {3, 2}, {5, 2}, M::Swimming).has_value(), "Ship cannot cross deep water");
        map.at({4, 3}).surface = S::ShallowWater;
        require(rts::findPath(map, {1, 2}, {7, 2}, M::Walking).has_value(), "Walker cannot use ford");
        map.at({1, 0}).surface = S::DeepWater;
        require(!map.canStep({0, 0}, {1, 1}, M::Walking), "Walker cut deep-water corner");
        for (int y = 0; y < 7; ++y) { map.at({6, y}).height = 2; map.occupy({6, y}); }
        require(!rts::findPath(map, {1, 2}, {7, 2}, M::Amphibious), "Amphibian ignored cliff/objects");
        require(rts::findPath(map, {1, 2}, {7, 2}, M::Flying).has_value(), "Air blocked by ground objects");
        require(!map.canStep({0, 0}, {-1, 0}, M::Flying), "Air escaped map");
    });
    test("Water simulation, movement-aware production, rally and dry construction", [] {
        using M = rts::MovementType; using S = rts::Surface;
        for (const M movement : {M::Walking, M::Swimming, M::Amphibious, M::Flying}) {
            rts::Scenario s{rts::Map(16, 12), {1, 1}, {4, 5}, {}};
            for (int y = 0; y < 12; ++y) for (int x = 4; x < 14; ++x)
                s.map.at({x, y}).surface = (x >= 7 && x <= 9) ? S::DeepWater : S::ShallowWater;
            rts::EntityDefinition type; type.movement = movement;
            auto game = makeTestSimulation(std::move(s), {}, type);
            require(!game.canPlace("church.hall", {5, 5}), "Building accepted shallow water");
            require(game.setRally(game.buildings()[0].id, {8, 5}) == (movement != M::Walking), "Rally ignored trained unit movement");
            require(game.command({12, 5}), "Water order rejected");
            ticks(game, 900);
            if (movement == M::Walking) require(game.worker().cell.x < 7, "Walker crossed deep water");
            else require(game.worker().cell == rts::Cell{12, 5}, "Non-walker did not cross deep water");
            if (movement == M::Flying) require(game.unitHeight(game.worker()) == 5, "Flight altitude follows cliffs");
        }
        auto s = flatScenario(); s.slots[0].startingCrystals = 100; s.worker = {5, 3};
        for (int y = 0; y < 10; ++y) s.map.at({4, y}).surface = S::ShallowWater;
        rts::EntityDefinition ship; ship.id = "test.ship"; ship.movement = M::Swimming; ship.canBuild = false; ship.trainingTicks = 1;
        auto dock = testDepot("dock", ship.id);
        auto game = makeTestSimulation(std::move(s), {}, {}, {dock, rts::EntityDefinition{}, ship});
        require(game.buildings()[0].rally == rts::Cell{4, 2}, "Default ship rally ignored water or perimeter order");
        require(game.setRally(game.buildings()[0].id, {4, 6}), "Ship rally rejected");
        require(!game.setRally(game.buildings()[0].id, {5, 6}), "Ship rally accepted land");
        require(game.train(game.buildings()[0].id), "Ship training failed");
        ticks(game, 300);
        require(game.units().size() == 2 && game.units()[1].cell == rts::Cell{4, 6}, "Ship did not spawn in water and reach rally");
    });
    test("Flying units share ground cells but reserve their own flight layer", [] {
        auto s = flatScenario(); s.slots[0].startingCrystals = 100;
        rts::EntityDefinition flyer; flyer.id = "test.fly"; flyer.movement = rts::MovementType::Flying; flyer.trainingTicks = 1;
        auto hall = testDepot("hall", flyer.id);
        auto game = makeTestSimulation(std::move(s), {}, {}, {hall, rts::EntityDefinition{}, flyer});
        require(game.setRally(game.buildings()[0].id, game.worker().cell), "Air rally rejected ground unit");
        game.train(game.buildings()[0].id); ticks(game, 300);
        require(game.units().size() == 2 && game.units()[1].cell == game.worker().cell, "Ground unit blocked air");
        game.train(game.buildings()[0].id); ticks(game, 300);
        require(game.units().size() == 3 && separated(game.units()[1], game.units()[2]), "Air units overlapped");
    });
}
}
