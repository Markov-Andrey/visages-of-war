#include "TestSupport.hpp"

namespace rts::tests {
void movementTests(TestSuite& test, const TestContext& context) {
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
    test("Circular path obstacles allow diagonal clearance and account for different sizes", [] {
        rts::Map map(12, 12);
        const std::array<rts::Cell, 1> goal{{{6, 6}}};
        std::array<rts::Circle, 1> obstacle{{{{5.5f, 4.5f}, .2f}}};
        const auto small = rts::findUnitPath(map, {4.5f, 4.5f}, goal, obstacle, .2f, rts::MovementType::Walking);
        obstacle[0].radius = .5f;
        const auto large = rts::findUnitPath(map, {4.5f, 4.5f}, goal, obstacle, .5f, rts::MovementType::Walking);
        require(small && large && small->cost == 28 && large->cost > small->cost, "Path still uses square unit occupancy");
        require(rts::sweptCircleIntersects({0, 0}, {5, 0}, .1f, {{2.5f, 0}, .1f}), "Fast circle tunnelled through unit");
        require(!rts::sweptCircleIntersects({0, 0}, {5, 0}, .1f, {{2.5f, .3f}, .1f}), "Broad phase replaced circular collision");
    });
    test("A moving circle may share a grid cell without intersecting a stationary circle", [] {
        rts::Scenario s{rts::Map(20, 16), {1, 1}, {5, 5}, {}};
        s.extraWorkers = {{7, 6}};
        rts::EntityDefinition type; type.collisionRadius = .1f;
        rts::Simulation game(std::move(s), {}, type);
        const auto id = game.worker().id;
        require(game.order(std::span<const rts::EntityId>(&id, 1), rts::OrderKind::Move, {12, 7}), "Order rejected");
        bool shared = false;
        for (int tick = 0; tick < 250; ++tick) {
            game.tick();
            require(separated(game.units()[0], game.units()[1]), "Circles overlapped");
            shared |= game.units()[0].cell == game.units()[1].cell;
        }
        require(shared, "Units still reserve entire grid cells");
        require(game.worker().cell == rts::Cell{12, 7} && game.worker().state == rts::UnitState::Idle, "Circle failed to reach goal");
    });
    test("Different circle sizes remain separated through crossing and replacement orders", [] {
        rts::Scenario s{rts::Map(24, 20), {1, 1}, {5, 8}, {}};
        rts::EntityDefinition large; large.id = "test.large"; large.collisionRadius = .5f; large.movementPerSecond = 15;
        s.units = {{large.id, 0, {12, 8}}};
        rts::Simulation game(std::move(s), {}, {}, {large});
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
                require(std::hypot(pa.x - pb.x, pa.y - pb.y) + .00001f >=
                    game.unit(a)->definition.collisionRadius + game.unit(b)->definition.collisionRadius,
                    "Moving circles intersected between simulation ticks");
            }
        }
        require(game.unit(a)->cell == rts::Cell{18, 8} && game.unit(b)->cell == rts::Cell{4, 8}, "Crossing traffic stalled");
    });
    test("Mid-step replacement orders and stop do not teleport", [] {
        rts::Simulation game(flatScenario());
        game.command({8, 3});
        ticks(game, 3);
        auto previous = game.worker().position;
        game.command({4, 8});
        for (int i = 0; i < 100; ++i) {
            game.tick();
            const auto now = game.worker().position;
            require(std::hypot(now.x - previous.x, now.y - previous.y) < 0.1f, "Teleport after replacement order");
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
    test("Twelve units descend, cross a lowered ford and form up on the other bank", [] {
        auto s = shoreScenario();
        for (int x = 4; x < 10; ++x) for (int y : {10, 11}) if (rts::Cell{x, y} != s.worker) s.extraWorkers.push_back({x, y});
        rts::Simulation game(std::move(s));
        std::vector<rts::EntityId> ids;
        std::vector<rts::FormationMember> members;
        for (const auto& u : game.units()) { ids.push_back(u.id); members.push_back({u.id, u.cell}); }
        const auto expected = rts::planFormation(game.map(), members, {26, 10}, {});
        require(game.command(ids, {26, 10}), "Ford group order rejected");
        std::vector<rts::EntityId> crossed;
        float previousHeight = game.unitHeight(game.worker());
        for (int tick = 0; tick < 1500; ++tick) {
            game.tick();
            const float height = game.unitHeight(game.worker());
            require(std::abs(height - previousHeight) < .15f, "Ground height snapped on shore ramp");
            previousHeight = height;
            for (size_t i = 0; i < game.units().size(); ++i) {
                const auto& u = game.units()[i];
                if (game.map().at(u.cell).surface == rts::Surface::ShallowWater && std::find(crossed.begin(), crossed.end(), u.id) == crossed.end()) crossed.push_back(u.id);
                for (size_t j = i + 1; j < game.units().size(); ++j) require(separated(u, game.units()[j]), "Ford traffic overlapped");
            }
        }
        require(crossed.size() == ids.size(), "Some units bypassed the lowered ford");
        for (const auto& slot : expected) require(rts::lengthSquared(game.unit(slot.id)->position - rts::center({26, 10})) < 9 && game.unit(slot.id)->state == rts::UnitState::Idle, "Squad stalled at shore or failed to reform");
    });
    test("Negative water elevation loads independently and rejects submerged ramps", [&] {
        const auto file = rts::Paths::executable().parent_path() / L"test-shore.rtsmap";
        const auto write = [&](nlohmann::json ramp) {
            auto j=mapJson(); j["terrain"]["heights"][2]="00--00"; j["terrain"]["heights"][3]="00--00";
            j["terrain"]["surfaces"][2]="LLSDLL"; j["terrain"]["surfaces"][3]="LLLSLL";
            j["terrain"]["ramps"]=nlohmann::json::array({ramp}); writeMap(file,j);
        };
        write({2,3,0,1});
        const auto s = loadScenario(file);
        require(s.map.at({2, 2}).height == -1 && s.map.at({3, 2}).height == -1 && s.map.at({2, 3}).height == -1, "Negative terrain glyph not decoded");
        require(s.map.at({2, 3}).surface == rts::Surface::Land && s.map.canStep({2, 4}, {2, 3}) && s.map.canStep({2, 3}, {2, 2}), "Shore ramp did not connect water to land");
        write({2,2,0,-1}); mustThrow([&] { loadScenario(file); });
        write({3,2,0,-1}); mustThrow([&] { loadScenario(file); });
        write({2,3,1,0}); mustThrow([&] { loadScenario(file); });
    });
    test("Lowered water picking and vision use the same elevation as terrain", [] {
        using S = rts::Surface;
        rts::Map map(9, 9);
        for (int y = 0; y < 9; ++y) for (int x = 0; x < 9; ++x) {
            map.at({x, y}).height = -1;
            map.at({x, y}).surface = x < 5 ? S::ShallowWater : S::DeepWater;
        }
        map.at({3, 3}).height = 0; map.at({3, 3}).surface = S::Land;
        for (float zoom : {.4f, 1.0f, 1.8f}) {
            const rts::WorldView view{{80, 140}, zoom};
            require(!map.pick(view.project({3.5f, 4}, -.5f), view), "Clicked through lowered shoreline cliff");
            require(map.pick(view.project(rts::center({3, 4}), -1), view) == rts::Cell{3, 4}, "Shallow water selected at wrong height");
            require(map.pick(view.project(rts::center({6, 4}), -1), view) == rts::Cell{6, 4}, "Deep water selected at different height");
            require(!map.pick(view.project({4.5f, 9}, -1.3f), view), "Water edge skirt became selectable");
        }
        require(rts::visionReaches(map, {{3, 3}, 8}, {6, 4}), "Shore cannot see lower water");
        require(rts::visionReaches(map, {{3, 4}, 8}, {6, 4}), "Water depth blocked vision");
        require(!rts::visionReaches(map, {{3, 4}, 8}, {3, 3}), "Lowered water ignored high ground vision rules");
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
            rts::Simulation game(std::move(s), {}, type);
            require(!game.canPlace("human.hall", {5, 5}), "Building accepted shallow water");
            require(game.setRally(game.buildings()[0].id, {8, 5}) == (movement != M::Walking), "Rally ignored trained unit movement");
            require(game.command({12, 5}), "Water order rejected");
            ticks(game, 900);
            if (movement == M::Walking) require(game.worker().cell.x < 7, "Walker crossed deep water");
            else require(game.worker().cell == rts::Cell{12, 5}, "Non-walker did not cross deep water");
            if (movement == M::Flying) require(game.unitHeight(game.worker()) == 5, "Flight altitude follows cliffs");
        }
        auto s = flatScenario(); s.startingCrystals = 100; s.worker = {5, 3};
        for (int y = 0; y < 10; ++y) s.map.at({4, y}).surface = S::ShallowWater;
        rts::EntityDefinition ship; ship.id = "test.ship"; ship.movement = M::Swimming; ship.canBuild = false; ship.trainingTicks = 1;
        auto dock = testDepot("dock", ship.id);
        rts::Simulation game(std::move(s), {}, {}, {dock, rts::EntityDefinition{}, ship});
        require(game.buildings()[0].rally == rts::Cell{4, 2}, "Default ship rally ignored water or perimeter order");
        require(game.setRally(game.buildings()[0].id, {4, 6}), "Ship rally rejected");
        require(!game.setRally(game.buildings()[0].id, {5, 6}), "Ship rally accepted land");
        require(game.train(game.buildings()[0].id), "Ship training failed");
        ticks(game, 300);
        require(game.units().size() == 2 && game.units()[1].cell == rts::Cell{4, 6}, "Ship did not spawn in water and reach rally");
    });
    test("Flying formation crosses terrain and ground troops while reserving adjacent air slots", [] {
        rts::Scenario s{rts::Map(36, 24), {1, 1}, {24, 10}, {}};
        for (int y = 0; y < 24; ++y) { s.map.at({18, y}).height = 3; s.map.occupy({18, y}); }
        rts::EntityDefinition flyer; flyer.id = "test.flyer"; flyer.movement = rts::MovementType::Flying;
        flyer.canBuild = false; flyer.carryCapacity = 0; flyer.movementPerSecond = 3.2f;
        for (int rank = 0; rank < 6; ++rank) for (int side = 0; side < 2; ++side) s.units.push_back({flyer.id, 0, {7 + rank, 9 + side}});
        rts::Simulation game(std::move(s), {}, {}, {flyer});
        std::vector<rts::EntityId> ids; std::vector<rts::FormationMember> members;
        for (const auto& u : game.units()) if (rts::airborne(u.definition.movement)) {
            ids.push_back(u.id); members.push_back({u.id, u.cell, u.definition.movement, u.definition.formationPriority});
        }
        const auto expected = rts::planFormation(game.map(), members, {24, 10}, {});
        require(game.command(ids, {24, 10}), "Air group order failed");
        bool crossedGround = false;
        for (int tick = 0; tick < 600; ++tick) {
            game.tick();
            for (size_t i = 0; i < ids.size(); ++i) {
                const auto* a = game.unit(ids[i]);
                crossedGround |= a->cell == game.worker().cell;
                for (size_t j = i + 1; j < ids.size(); ++j) {
                    const auto* b = game.unit(ids[j]);
                    require(separated(*a, *b), "Flying group collided within its own layer");
                }
            }
        }
        require(expected.size() == 12 && crossedGround, "Air units avoided ground occupancy");
        for (const auto& slot : expected) require(rts::lengthSquared(game.unit(slot.id)->position - slot.position) < 16 && game.unit(slot.id)->state == rts::UnitState::Idle, "Flyers did not reform near their destination");
        const auto* first = game.unit(ids.front());
        const rts::WorldView view{{100, 150}, 1};
        const auto flyingPoint = rts::unitScreenAnchor(view, first->position, game.unitHeight(*first)) + rts::Vec2{0, -25};
        require(rts::pickEntity(game, view, flyingPoint) == first->id, "Flying sprite is not selectable at altitude");
    });
    test("Flying units share ground cells but reserve their own flight layer", [] {
        auto s = flatScenario(); s.startingCrystals = 100;
        rts::EntityDefinition flyer; flyer.id = "test.fly"; flyer.movement = rts::MovementType::Flying; flyer.trainingTicks = 1;
        auto hall = testDepot("hall", flyer.id);
        rts::Simulation game(std::move(s), {}, {}, {hall, rts::EntityDefinition{}, flyer});
        require(game.setRally(game.buildings()[0].id, game.worker().cell), "Air rally rejected ground unit");
        game.train(game.buildings()[0].id); ticks(game, 300);
        require(game.units().size() == 2 && game.units()[1].cell == game.worker().cell, "Ground unit blocked air");
        game.train(game.buildings()[0].id); ticks(game, 300);
        require(game.units().size() == 3 && separated(game.units()[1], game.units()[2]), "Air units overlapped");
    });
}
}
