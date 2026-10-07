#include "TestSupport.hpp"
#include <future>

namespace rts::tests {
void navigationTests(TestSuite& test, const TestContext& context) {
    (void)context;
    test("Water bed slopes continuously while the surface and navigation stay flat", [] {
        Map map(8, 4);
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) {
            auto& t = map.at({x, y}); t.height = -1;
            t.surface = x < 2 ? Surface::Land : x < 5 ? Surface::ShallowWater : Surface::DeepWater;
        }
        require(std::abs(map.bedHeight({3, 1}, {3.5f, 1.5f}) + 1.8f) < .0001f, "Wrong shallow bed");
        require(std::abs(map.bedHeight({6, 1}, {6.5f, 1.5f}) + 2.8f) < .0001f, "Deep bed is not one level lower");
        for (float x = 1.5f; x < 6.5f; x += .01f) {
            const Cell c{int(x), 1}, next{int(x + .01f), 1};
            require(std::abs(map.bedHeight(c, {x, 1.5f}) - map.bedHeight(next, {x + .01f, 1.5f})) < .02f,
                "Bed snapped at a dry/shallow/deep seam");
            require(map.surfaceHeight(c, {x, 1.5f}) == -1, "Water surface followed the bed");
        }
        require(map.canStep({1, 1}, {2, 1}) && !map.canStep({4, 1}, {5, 1}), "Wading changed walkability");
        require(map.canStep({4, 1}, {5, 1}, MovementType::Swimming), "Bed slope blocked a swimmer");
        require(map.movementHeight({6.5f, 1.5f}, MovementType::Flying) == 5, "Water lowered a flyer");
        require(std::abs(map.movementHeight({6.5f, 1.5f}, MovementType::Swimming) + 1.28f) < .0001f,
            "Swimmer sank to the deep bed");
        const WorldView view{{30, 20}, 1.2f};
        require(map.pick(view.project({6.5f, 1.5f}, -1), view) == Cell{6, 1}, "Water picking followed submerged bed");
        map.at({0, 0}).height = 1;
        require(map.waterDepth({0, 0}, {.9f, .9f}) == 0, "Separate elevation acquired water");
        map.at({3, 1}).surface = Surface::Land;
        require(map.waterDepth({4, 2}, {4.001f, 2.001f}) < .001f,
            "Diagonal entry into water snapped at a bank corner");
    });
    test("A* endpoints, diagonal cost, unreachable and occupied targets", [] {
        rts::Map map(5, 5);
        auto path = rts::findPath(map, {0, 0}, {4, 4});
        require(path && path->cost == 56 && path->cells.size() == 5, "Incorrect diagonal route");
        require(path->cells.front() == rts::Cell{0, 0} && path->cells.back() == rts::Cell{4, 4}, "Incorrect endpoints");
        require(rts::findPath(map, {2, 2}, {2, 2})->cost == 0, "Start equals goal");
        map.at({4, 4}).blocked = true;
        require(!rts::findPath(map, {0, 0}, {4, 4}), "Occupied target accepted");
        require(!rts::findPath(map, {-1, 0}, {1, 1}), "Out of bounds accepted");
        map.at({1, 0}).blocked = true;
        map.at({0, 1}).blocked = true;
        require(!rts::findPath(map, {0, 0}, {3, 3}), "Cut through a corner");
    });
    test("A* optimality against Dijkstra on 150 seeded obstacle maps", [] {
        std::mt19937 random(56122);
        for (int iteration = 0; iteration < 150; ++iteration) {
            rts::Map map(12, 12);
            for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) map.at({x, y}).blocked = random() % 5 == 0;
            map.at({0, 0}).blocked = false;
            map.at({11, 11}).blocked = false;
            const int expected = oracleCost(map, {0, 0}, {11, 11});
            const auto actual = rts::findPath(map, {0, 0}, {11, 11});
            require(actual ? actual->cost == expected : expected == -1, "Non-optimal A* route");
            if (actual) for (size_t i = 1; i < actual->cells.size(); ++i)
                require(map.canStep(actual->cells[i - 1], actual->cells[i]), "Invalid path edge");
        }
    });
    test("Repeated searches isolate occupancy terrain dimensions movement and early exits", [] {
        for (int size : {32, 5, 48, 5, 32}) {
            rts::Map map(size, size);
            const rts::Cell goal{size - 2, 1};
            const std::array<rts::Cell, 1> goals{{goal}}, occupied{{goal}};
            const auto expected = rts::findPath(map, {1, 1}, goal);
            require(expected && expected->cost == (size - 3) * 10, "Unexpected open route");
            require(!rts::findPath(map, {1, 1}, goals, occupied), "Occupied target accepted");
            const auto afterOccupied = rts::findPath(map, {1, 1}, goal);
            require(afterOccupied && afterOccupied->cells == expected->cells, "Previous occupancy leaked into search");
            require(!rts::findUnitPathTo(map, {1.5f, 1.5f}, rts::center(goal), {}, .35f, rts::MovementType::Walking, 1), "Search ignored expansion limit");
            const auto afterLimited = rts::findPath(map, {1, 1}, goal);
            require(afterLimited && afterLimited->cells == expected->cells, "Limited search leaked costs or parents");
            for (int y = 0; y < size; ++y) map.at({size / 2, y}).blocked = true;
            require(!rts::findPath(map, {1, 1}, goal), "Changed terrain was ignored");
            require(rts::findPath(map, {1, 1}, goal, rts::MovementType::Flying).has_value(), "Ground search blocked air route");
            require(!rts::findPath(map, {1, 1}, goal), "Air search opened ground route");
            map.at({size / 2, 1}).blocked = false;
            const auto reopened = rts::findPath(map, {1, 1}, goal);
            require(reopened && reopened->cells == expected->cells, "Unreachable search poisoned later route");
        }
    });
    test("Concurrent path searches keep independent scratch state", [] {
        const auto search = [](int size) {
            rts::Map map(size, size);
            for (int i = 0; i < 80; ++i) {
                const auto path = rts::findPath(map, {0, 0}, {size - 1, size - 1});
                if (!path || path->cost != (size - 1) * 14 || path->cells.size() != static_cast<size_t>(size)) return false;
            }
            return true;
        };
        auto first = std::async(std::launch::async, search, 24);
        auto second = std::async(std::launch::async, search, 37);
        require(first.get() && second.get(), "Parallel searches shared mutable buffers");
    });
    test("Cliffs are impassable; ramps work both ways without side entry", [] {
        rts::Map map(7, 5);
        for (int y = 0; y < 5; ++y) for (int x = 3; x < 7; ++x) map.at({x, y}).height = 1;
        require(!rts::findPath(map, {0, 2}, {6, 2}), "Walked through a cliff");
        map.at({2, 2}).ramp = {1, 0};
        const auto up = rts::findPath(map, {0, 2}, {6, 2});
        const auto down = rts::findPath(map, {6, 2}, {0, 2});
        require(up && down && up->cost == 60 && down->cost == 60, "Ramp not bidirectional");
        require(!map.canStep({2, 1}, {2, 2}) && !map.canStep({2, 2}, {2, 3}), "Side entrance to ramp");
        require(!map.canStep({2, 2}, {3, 3}), "Diagonal ramp shortcut");
        map.at({2, 2}).blocked = true;
        require(!rts::findPath(map, {0, 2}, {6, 2}), "Blocked ramp still usable");
    });
    test("Wide ramps connect lateral lanes without allowing side entry from flat ground", [] {
        for (int level : {-1, 0}) for (rts::Cell direction : {rts::Cell{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            rts::Map map(9, 9);
            for (int y = 0; y < 9; ++y) for (int x = 0; x < 9; ++x) map.at({x, y}).height = level;
            const rts::Cell middle{4, 4}, across{-direction.y, direction.x};
            for (int lane = -1; lane <= 1; ++lane) {
                const auto c = middle + rts::Cell{across.x * lane, across.y * lane};
                map.at(c).ramp = direction; map.at(c + direction).height = level + 1;
                require(map.canStep(c - direction, c) && map.canStep(c, c + direction) &&
                    map.canStep(c + direction, c) && map.canStep(c, c - direction), "Wide ramp lane cannot be traversed");
            }
            require(map.canStep(middle - across, middle) && map.canStep(middle, middle + across) &&
                map.canStep(middle + across, middle), "Adjacent ramp lanes disconnected");
            const auto path = rts::findPath(map, middle - across, middle + across);
            require(path && path->cost == 20, "Unit detoured around continuous ramp surface");
            const auto edge = rts::center(middle) + rts::Vec2{float(across.x), float(across.y)} * .5f;
            require(map.surfaceHeight(middle, edge) == map.surfaceHeight(middle + across, edge), "Adjacent lanes have a vertical seam");
            const auto outside = middle + rts::Cell{across.x * 2, across.y * 2};
            require(!map.canStep(outside, middle + across) && !map.canStep(middle + across, outside), "Unit jumped into ramp from its side");
            map.at(middle - direction).ramp = direction;
            require(!map.canStep(middle - direction, middle), "Equal-height consecutive ramps hide a cliff");
        }
    });
    test("Gather positions do not reach through cliffs", [] {
        rts::Map map(5, 5);
        map.at({2, 2}).height = 1;
        map.at({2, 2}).blocked = true;
        require(rts::perimeter(map, {2, 2}, 1, 1).empty(), "Gathered up a cliff");
        map.at({3, 2}).height = 1;
        const auto targets = rts::perimeter(map, {2, 2}, 1, 1);
        require(targets.size() == 1 && targets[0] == rts::Cell{3, 2}, "Wrong gather perimeter");
    });
    test("Multi-goal search chooses closest reachable perimeter", [] {
        rts::Map map(7, 7);
        map.at({2, 2}).blocked = true;
        const std::array<rts::Cell, 3> goals{{{6, 6}, {2, 2}, {1, 0}}};
        const auto path = rts::findPath(map, {0, 0}, goals);
        require(path && path->cost == 10 && path->cells.back() == rts::Cell{1, 0}, "Wrong goal selected");
    });
    test("Shared route fields preserve shortest terrain routes and invalidate blocked edges", [] {
        auto s = shoreScenario();
        const std::array<rts::Cell, 2> goals{{{26, 10}, {26, 11}}};
        for (auto movement : {rts::MovementType::Walking, rts::MovementType::Amphibious, rts::MovementType::Flying}) {
            const auto field = rts::makeRouteField(s.map, goals, movement);
            for (rts::Cell start : {rts::Cell{4, 10}, {6, 11}, {9, 9}}) {
                const auto expected = rts::findPath(s.map, start, goals, {}, movement);
                require(expected && field.costs[start.y * s.map.width() + start.x] == expected->cost, "Shared field changed terrain reachability/cost");
                const auto route = rts::fieldPath(s.map, field, rts::center(start), expected->cells.back(), .35f);
                require(route && route->cost == expected->cost, "Group field extraction missed a ramp");
                for (size_t i = 1; i < route->cells.size(); ++i) require(s.map.canStep(route->cells[i - 1], route->cells[i], movement), "Field cut a corner");
            }
        }
        rts::Map corridor(8, 3);
        for (int x = 0; x < 8; ++x) { corridor.at({x, 0}).blocked = true; corridor.at({x, 2}).blocked = true; }
        const std::array<rts::Cell, 1> end{{{6, 1}}};
        const auto field = rts::makeRouteField(corridor, end, rts::MovementType::Walking);
        corridor.occupy({4, 1});
        require(!rts::fieldPath(corridor, field, {1.5f, 1.5f}, end[0], .35f), "Stale field crossed a newly built wall");
    });
    test("Continuous path goals need no free cell centre", [] {
        rts::Map map(12, 12);
        const std::array<rts::Circle, 1> held{{{{6.5f, 6.5f}, .1f}}};
        const auto path = rts::findUnitPathTo(map, {2.5f, 6.5f}, {6.1f, 6.1f}, held, .1f, rts::MovementType::Walking);
        require(path && path->destination == rts::Vec2{6.1f, 6.1f}, "Fractional endpoint inherited centre occupancy");
        const auto inside = rts::findUnitPathTo(map, {6.1f, 6.2f}, {6.1f, 6.1f}, held, .1f, rts::MovementType::Walking);
        require(inside && inside->cells.size() == 1, "Cannot move inside the same grid cell");
        map.occupy({7, 6});
        require(!rts::findUnitPathTo(map, {2.5f, 6.5f}, {6.9f, 6.5f}, {}, .35f, rts::MovementType::Walking), "Fractional goal clips square building");
    });
    test("Projection and picking account for camera, elevation and ramp surface", [] {
        rts::WorldView view{{310, 120}, 1.35f};
        const auto zero = view.project({0, 0}), right = view.project({1, 0}), down = view.project({0, 1});
        require(right.x > zero.x && right.y == zero.y && down.x == zero.x && down.y > zero.y, "Terrain grid axes are not screen-aligned");
        require(std::abs(right.x - zero.x - (down.y - zero.y)) < .001f, "Terrain cells are not square");
        const rts::Vec2 world{6.25f, 3.5f};
        const auto back = view.unproject(view.project(world, 2), 2);
        require(std::abs(back.x - world.x) < 0.001f && std::abs(back.y - world.y) < 0.001f, "Projection roundtrip");
        rts::Map map(8, 8);
        map.at({3, 3}).height = 1;
        const auto screen = view.project(rts::center({3, 3}), 1);
        require(map.pick(screen, view) == rts::Cell{3, 3}, "Wrong elevated tile picked");
        map.at({2, 3}).ramp = {1, 0};
        require(map.pick(view.project(rts::center({2, 3}), 0.5f), view) == rts::Cell{2, 3}, "Wrong ramp picked");
    });
}
}
