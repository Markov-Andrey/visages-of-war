#include "TestSupport.hpp"

namespace rts::tests {
namespace {
void checkMotion(const Simulation& game) {
    for (size_t i = 0; i < game.units().size(); ++i) {
        const auto& a = game.units()[i];
        require(game.map().canTraverse(a.tickPosition, a.position, a.definition.collisionRadius, a.definition.movement), "Crowd crossed square terrain or a cliff");
        require(groundLengthSquared(a.position - a.tickPosition) <= std::pow(a.definition.movementPerSecond / Simulation::ticksPerSecond + .00001f, 2), "Crowd teleported");
        for (size_t j = i + 1; j < game.units().size(); ++j) {
            const auto& b = game.units()[j];
            if (airborne(a.definition.movement) != airborne(b.definition.movement)) continue;
            require(!sweptCircleIntersects(a.tickPosition - b.tickPosition, a.position - b.position,
                a.definition.collisionRadius, {{}, b.definition.collisionRadius}), "Circles intersected during a tick");
        }
    }
}
std::vector<EntityId> selected(const Simulation& game) {
    std::vector<EntityId> ids;
    for (const auto& u : game.units()) if (u.owner == game.player().id) ids.push_back(u.id);
    return ids;
}
bool settled(const Simulation& game) {
    return std::all_of(game.units().begin(), game.units().end(), [](const Unit& u) { return u.state == UnitState::Idle; });
}
}
void formationTests(TestSuite& test, const TestContext& context) {
    (void)context;
    test("Mixed roles actually finish in front-to-back bands after movement and reversal", [] {
        for (Cell direction : {Cell{1, 0}, {1, 1}, {-1, 0}, {1, -1}, {0, -1}}) {
            Scenario site{Map(64, 64), {1, 1}, {29, 29}, {}};
            EntityDefinition melee; melee.formationPriority = 1; melee.collisionRadius = .4f;
            auto slinger = melee; slinger.id = "slinger"; slinger.formationPriority = 2;
            auto siege = melee; siege.id = "siege"; siege.formationPriority = 3; siege.movementPerSecond = 2;
            for (int i = 1; i < 12; ++i) site.units.push_back({i % 3 == 0 ? melee.id : i % 3 == 1 ? slinger.id : siege.id,
                0, {29 + i % 4, 29 + i / 4}});
            auto game = makeTestSimulation(std::move(site), {}, melee, {melee, slinger, siege});
            auto ids = selected(game); std::reverse(ids.begin(), ids.end());
            for (int sign : {1, -1}) {
                const Cell target{31 + sign * direction.x * 12, 31 + sign * direction.y * 12};
                require(game.command(ids, target), "Mixed role movement rejected");
                float distance = 0;
                for (const auto& u : game.units()) distance = std::max(distance, groundLength(u.routeDestination - u.position));
                const int deadline = int(std::ceil(distance / siege.movementPerSecond * Simulation::ticksPerSecond)) + 400;
                for (int tick = 0; tick < deadline && !settled(game); ++tick) { game.tick(); checkMotion(game); }
                if (!settled(game)) for (const auto& u : game.units()) std::cerr << "mixed " << direction.x << ',' << direction.y << " sign=" << sign
                    << " role=" << u.definition.formationPriority << " pos=" << u.position.x << ',' << u.position.y
                    << " goal=" << u.routeDestination.x << ',' << u.routeDestination.y << " state=" << int(u.state)
                    << " blocked=" << u.blockedTicks << " next=" << u.next << '/' << u.route.size() << '\n';
                require(settled(game), "Mixed group did not finish reforming");
                for (const auto& a : game.units()) for (const auto& b : game.units()) if (a.definition.formationPriority < b.definition.formationPriority) {
                    const Vec2 delta = a.position - b.position;
                    require(groundDot(delta, a.formationForward) >= -.05f, "Rear role finished ahead of front role");
                }
                std::vector<Vec2> positions;
                for (const auto& u : game.units()) positions.push_back(u.position);
                ticks(game, 60);
                for (size_t i = 0; i < positions.size(); ++i) require(game.units()[i].position == positions[i], "Settled roles kept yielding");
            }
        }
    });
    test("Circular packing respects square footprints, terrain connectivity and held circles", [] {
        Map map(20, 20);
        for (int y = 0; y < 20; ++y) map.at({12, y}).blocked = true;
        map.occupy({9, 10});
        std::vector<FormationMember> members{{1, {3, 5}}, {2, {4, 5}}, {3, {3, 6}}, {4, {4, 6}}};
        const std::array<FormationObstacle, 1> held{{{{10, 9}, false, Vec2{10.2f, 9.9f}, .5f}}};
        const auto slots = planFormation(map, members, {11, 10}, held);
        require(slots.size() == members.size(), "Obstacle dropped group members");
        for (const auto& slot : slots) {
            require(slot.cell.x < 12 && map.canTraverse(slot.position, slot.position, .35f), "Disk clips a square obstacle");
            require(!sweptCircleIntersects(slot.position, slot.position, .35f, {*held[0].position, held[0].radius}), "Held circle ignored");
            require(findUnitPathTo(map, center(members[slot.id - 1].start), slot.position, {}, .35f, MovementType::Walking).has_value(), "Unreachable circle destination");
        }
    });
    test("Mixed movement groups receive reachable continuous destinations", [] {
        Map map(40, 32);
        for (int y = 0; y < 32; ++y) for (int x = 18; x < 40; ++x) map.at({x, y}).surface = Surface::DeepWater;
        const std::vector<FormationMember> members{{1, {5, 14}, MovementType::Walking, 1}, {2, {20, 14}, MovementType::Swimming, 1},
            {3, {5, 15}, MovementType::Amphibious, 2}, {4, {5, 16}, MovementType::Flying, 2}};
        const auto slots = planFormation(map, members, {18, 20}, {});
        require(slots.size() == members.size(), "Shoreline dropped a movement type");
        for (const auto& slot : slots) {
            const auto& m = members[slot.id - 1];
            require(findUnitPathTo(map, center(m.start), slot.position, {}, m.radius, m.movement).has_value(), "Mixed group received unreachable destination");
        }
    });
    test("Twelve-unit crowds arrive promptly in eight directions without circling settled friends", [] {
        for (Cell direction : {Cell{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}) {
            Scenario s{Map(64, 64), {1, 1}, {30, 30}, {}};
            for (int i = 1; i < 12; ++i) s.extraWorkers.push_back({30 + i % 4, 30 + i / 4});
            auto game = makeTestSimulation(std::move(s));
            auto ids = selected(game); std::reverse(ids.begin(), ids.end());
            const Cell target{31 + direction.x * 14, 31 + direction.y * 14};
            std::vector<float> distances(ids.size());
            require(game.command(ids, target), "Crowd order failed");
            int first = -1, tick = 0;
            for (; tick < 420; ++tick) {
                game.tick(); checkMotion(game);
                for (size_t i = 0; i < game.units().size(); ++i) {
                    const auto& u = game.units()[i];
                    distances[i] += std::sqrt(groundLengthSquared(u.position - u.tickPosition));
                    if (u.state == UnitState::Idle && first < 0) first = tick;
                }
                if (settled(game)) break;
            }
            if (!settled(game)) std::cerr << "crowd miss " << direction.x << ',' << direction.y << '\n';
            require(settled(game), "Crowd stalled on open terrain");
            require(tick - first <= 75, "Late members circled the arrived group");
            for (size_t i = 0; i < game.units().size(); ++i) {
                require(groundLengthSquared(game.units()[i].position - center(target)) < 9, "Crowd spread outside arrival region");
                require(distances[i] < (14 * groundLength({float(direction.x), float(direction.y)}) + 7.5f), "Unnecessary long detour around friends");
            }
            std::vector<Vec2> positions;
            for (const auto& u : game.units()) positions.push_back(u.position);
            ticks(game, 90);
            for (size_t i = 0; i < positions.size(); ++i) require(game.units()[i].position == positions[i], "Arrived crowd kept shuffling");
        }
    });
    test("Crowd compresses through a one-cell gate and reforms without overlap", [] {
        Scenario s{Map(64, 48), {1, 1}, {10, 19}, {}};
        for (int i = 1; i < 12; ++i) s.extraWorkers.push_back({10 + i % 4, 19 + i / 4});
        for (int y = 0; y < 48; ++y) if (y != 20) s.map.at({30, y}).blocked = true;
        auto game = makeTestSimulation(std::move(s));
        const auto ids = selected(game);
        require(game.command(ids, {46, 20}), "Gate order rejected");
        struct FacingTrace { Cell raw{0, 1}, shown{0, 1}; int rawTurn = -10, shownTurn = -10; };
        std::vector<FacingTrace> facings(game.units().size());
        int rawFlickers = 0, shownFlickers = 0;
        for (int tick = 0; tick < 900 && !settled(game); ++tick) {
            game.tick(); checkMotion(game);
            for (size_t i = 0; i < game.units().size(); ++i) {
                const auto& u = game.units()[i]; auto& trace = facings[i];
                if (u.next >= u.route.size()) continue; // Arrival has an explicit final group heading.
                const auto delta = u.position - u.tickPosition;
                Cell raw = trace.raw;
                if (groundLengthSquared(delta) > 1e-10f) {
                    constexpr std::array<Cell, 8> headings{{{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}}};
                    const int heading = int(std::lround(std::atan2(delta.y, delta.x) / .7853981634f));
                    raw = headings[(heading + 8) % 8];
                }
                const auto record = [&](Cell value, Cell& previous, int& lastTurn, int& flickers) {
                    if (value == previous) return;
                    if (tick - lastTurn < 3) ++flickers;
                    previous = value; lastTurn = tick;
                };
                record(raw, trace.raw, trace.rawTurn, rawFlickers);
                record(u.facing, trace.shown, trace.shownTurn, shownFlickers);
            }
        }
        std::cout << "  gate facing changes less than 100 ms apart: raw=" << rawFlickers << ", shown=" << shownFlickers << '\n';
        require(rawFlickers > 0, "Gate scenario no longer exercises rapid collision turns");
        require(shownFlickers == 0, "Collision steering flickered the sprite within 100 ms");
        require(settled(game), "Crowd jammed in a one-cell gate");
        for (const auto& u : game.units()) {
            require(u.position.x > 31, "Unit stopped on the wrong side of gate");
            require(groundLengthSquared(u.position - center({46, 20})) < 3.25f * 3.25f, "Crowd failed to gather after the gate");
        }
    });
    test("Dense followers respect a leader stopping or reversing", [] {
        for (bool reverse : {false, true}) {
            Scenario s{Map(40, 20), {1, 1}, {10, 10}, {}};
            s.extraWorkers = {{11, 10}, {12, 10}, {10, 11}, {11, 11}, {12, 11}};
            auto game = makeTestSimulation(std::move(s));
            const auto ids = selected(game);
            game.command(ids, {30, 10}); ticks(game, 40);
            const auto leader = game.units()[2].id;
            if (reverse) game.command(std::span<const EntityId>(&leader, 1), {8, 8});
            else game.stop(std::span<const EntityId>(&leader, 1));
            const auto stopped = game.unit(leader)->position;
            for (int tick = 0; tick < 600; ++tick) { game.tick(); checkMotion(game); }
            if (!reverse) require(game.unit(leader)->position == stopped, "Crowd pushed a stopped unit");
            else require(game.unit(leader)->cell == Cell{8, 8}, "Reversing leader failed to pass friends");
        }
    });
    test("A closed enemy surround holds and a body-sized opening permits escape", [] {
        for (bool open : {false, true}) {
            Scenario s{Map(24, 20), {1, 1}, {10, 10}, {}};
            EntityDefinition blocker; blocker.id = "blocker"; blocker.attackDamage = 0; blocker.collisionRadius = .4f;
            EntityDefinition victim; victim.attackDamage = 0;
            for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
                if ((!x && !y) || (open && x == 1 && y == 0)) continue;
                s.units.push_back({blocker.id, 1, {10 + x, 10 + y}});
            }
            auto game = makeTestSimulation(std::move(s), {}, victim, {blocker});
            const auto start = game.worker().position;
            const auto id = game.worker().id;
            game.command(std::span<const EntityId>(&id, 1), {17, 10});
            for (int tick = 0; tick < 300; ++tick) {
                game.tick(); checkMotion(game);
                for (size_t i = 1; i < game.units().size(); ++i) require(game.units()[i].position == center(game.units()[i].cell), "Enemy ring was displaced");
                if (!open) require(groundLengthSquared(game.worker().position - start) < .5f, "Unit escaped a closed surround");
            }
            if (open) require(game.worker().cell == Cell{17, 10} && game.worker().state == UnitState::Idle, "Unit failed to escape through open gap");
        }
    });
}
}
