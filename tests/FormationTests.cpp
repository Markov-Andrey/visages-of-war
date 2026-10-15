#include "TestSupport.hpp"

namespace rts::tests {
namespace {
void checkMotion(const Simulation& game) {
    for (size_t i = 0; i < game.units().size(); ++i) {
        const auto& a = game.units()[i];
        require(game.map().canTraverse(a.motion().tickPosition, a.position, a.definition.collisionRadius, a.definition.movement), "Crowd crossed square terrain or a cliff");
        // A 256-cell map needs a few coordinate ULPs of slack when subtracting
        // two float positions; the speed bound itself is independent of size.
        const float rounding = 4 * std::numeric_limits<float>::epsilon() *
            std::max({1.0f, std::abs(a.position.x), std::abs(a.position.y)});
        require(groundLengthSquared(a.position - a.motion().tickPosition) <= std::pow(a.definition.movementPerSecond / Simulation::ticksPerSecond + .00001f + rounding, 2), "Crowd teleported");
        for (size_t j = i + 1; j < game.units().size(); ++j) {
            const auto& b = game.units()[j];
            if (airborne(a.definition.movement) != airborne(b.definition.movement)) continue;
            require(!sweptCircleIntersects(a.motion().tickPosition - b.motion().tickPosition, a.position - b.position,
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
    return std::all_of(game.units().begin(), game.units().end(), [](const Unit& u) { return u.orders().state == UnitState::Idle; });
}
}
void formationTests(TestSuite& test, const TestContext& context) {
    (void)context;
    test("Blocked movement retries share a bounded fair queue and respect Stop and Hold", [] {
        Scenario s{Map(32, 96), {1, 1}, {10, 6}, {}};
        EntityDefinition worker; worker.attackDamage = 0;
        auto blocker = worker; blocker.id = "blocker"; blocker.collisionRadius = .4f;
        blocker.cost.supply = 0; // Stationary fixture bodies, independent of army supply.
        for (int i = 0; i < 20; ++i) {
            const Cell origin{10, 6 + i * 4};
            if (i) s.extraWorkers.push_back(origin);
            for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
                if (x || y) s.units.push_back({blocker.id, 1, origin + Cell{x, y}});
        }
        auto game = makeTestSimulation(std::move(s), {}, worker, {worker, blocker});
        const auto ids = selected(game);
        for (auto id : ids) require(game.order(std::array{id}, OrderKind::Move, {20, game.unit(id)->cell.y}), "Trapped order rejected");
        size_t peakQueue = 0; int requests = 0;
        for (int tick = 0; tick < 150; ++tick) {
            game.tick(); checkMotion(game);
            const auto& stats = game.movementStatistics();
            peakQueue = std::max(peakQueue, stats.pending); requests += stats.requests;
            require(stats.searches <= Simulation::movementSearchesPerTick, "Movement exceeded shared search budget");
        }
        require(peakQueue > 0 && requests >= 20, "Scenario did not exercise deferred path requests");
        for (auto id : ids) require(game.unit(id)->motion().replanAfter > 100, "A late request starved behind earlier units");
        game.order(std::span(ids).first(10), OrderKind::Stop);
        game.order(std::span(ids).subspan(10), OrderKind::Hold);
        std::vector<Vec2> positions;
        for (auto id : ids) positions.push_back(game.unit(id)->position);
        for (int tick = 0; tick < 60; ++tick) { game.tick(); checkMotion(game); }
        for (size_t i = 0; i < ids.size(); ++i)
            require(game.unit(ids[i])->position == positions[i] && !game.unit(ids[i])->motion().yieldingTo, "Deferred movement displaced Stop/Hold");
        require(game.movementStatistics().pending == 0, "Cancelled orders remained in the retry queue");
    });
    test("Ten independent armies cross and arrive in their own formations", [] {
        Scenario s{Map(128, 192), {1, 1}, {10, 12}, {}};
        EntityDefinition worker; worker.attackDamage = 0;
        for (PlayerId owner = 0; owner < maximumPlayers; ++owner) {
            s.slots[owner].state = owner ? PlayerSlotState::AI : PlayerSlotState::Player;
            for (int i = owner ? 0 : 1; i < 12; ++i)
                s.units.push_back({worker.id, owner, {10 + (owner % 2) * 56 + i % 4, 12 + (owner / 2) * 36 + i / 4}});
        }
        auto game = makeTestSimulation(std::move(s), {}, worker);
        for (PlayerId owner = 0; owner < maximumPlayers; ++owner) {
            std::vector<EntityId> ids;
            for (const auto& u : game.units()) if (u.owner == owner) ids.push_back(u.id);
            require(game.orderFor(owner, ids, OrderKind::Move, {28 + (owner % 2) * 56, 14 + (owner / 2) * 36}), "Army order rejected");
        }
        for (int tick = 0; tick < 700 && !settled(game); ++tick) { game.tick(); checkMotion(game); }
        require(settled(game), "One of ten armies did not arrive");
        for (const auto& u : game.units()) require(groundLength(u.position - center(u.motion().groupTarget)) <= u.motion().groupRadius + .71f, "Army failed to reform");
    });
    test("A thousand moving units preserve sweeps and the per-player supply limit", [] {
        Scenario s{Map(256, 256), {1, 1}, {10, 20}, {}};
        EntityDefinition worker; worker.attackDamage = 0;
        for (PlayerId owner = 0; owner < maximumPlayers; ++owner) {
            s.slots[owner].state = owner ? PlayerSlotState::AI : PlayerSlotState::Player;
            for (int i = owner ? 0 : 1; i < 100; ++i)
                s.units.push_back({worker.id, owner, {10 + (owner % 5) * 48 + (i % 10) * 2, 20 + (owner / 5) * 110 + (i / 10) * 2}});
        }
        auto game = makeTestSimulation(std::move(s), {}, worker);
        require(game.units().size() == 1000, "Supply limit was applied to the entire map");
        for (PlayerId owner = 0; owner < maximumPlayers; ++owner) {
            std::vector<EntityId> ids;
            for (const auto& u : game.units()) if (u.owner == owner) ids.push_back(u.id);
            require(game.orderFor(owner, ids, OrderKind::Move, {33 + (owner % 5) * 48, 29 + (owner / 5) * 110}), "Large army order rejected");
        }
        for (int tick = 0; tick < 60; ++tick) {
            game.tick(); checkMotion(game);
            require(game.movementStatistics().searches <= Simulation::movementSearchesPerTick, "Large armies exceeded retry budget");
        }
        require(game.movementStatistics().candidates < game.units().size() * game.units().size(), "Local movement scanned all thousand units");
    });
    test("Opposing orders cross narrow passages and both finish reforming", [] {
        for (int width : {1, 2, 3}) for (bool mixed : {false, true}) for (bool ramp : {false, true}) {
            Scenario s{Map(48, 36), {1, 1}, {10, 17}, {}};
            for (int x = mixed ? 17 : 21; x <= (mixed ? 30 : 26); ++x) for (int y = 0; y < 36; ++y)
                if (y < 17 || y >= 17 + width) s.map.at({x, y}).blocked = true;
            if (ramp) {
                for (int x = 25; x < 48; ++x) for (int y = 0; y < 36; ++y) s.map.at({x, y}).height = 1;
                for (int y = 17; y < 17 + width; ++y) s.map.at({24, y}).ramp = {1, 0};
            }
            EntityDefinition small; small.collisionRadius = mixed ? .4f : .35f; small.formationPriority = 1;
            auto large = small; large.id = "large"; large.collisionRadius = mixed ? .5f : .35f; large.formationPriority = mixed ? 2 : 1;
            for (int i = 1; i < 6; ++i) s.units.push_back({i % 2 ? large.id : small.id, 0, {10 + i % 3, 17 + i / 3}});
            for (int i = 0; i < 6; ++i) s.units.push_back({i % 2 ? large.id : small.id, 0, {35 + i % 3, 17 + i / 3}});
            auto game = makeTestSimulation(std::move(s), {}, small, {small, large});
            const auto ids = selected(game);
            require(game.order(std::span(ids).first(6), OrderKind::Move, {37, 18}), "First crossing order rejected");
            require(game.order(std::span(ids).subspan(6), OrderKind::Move, {10, 18}), "Second crossing order rejected");
            bool yielded = false;
            for (int tick = 0; tick < 1800 && !settled(game); ++tick) {
                game.tick(); checkMotion(game);
                for (const auto& u : game.units()) yielded |= u.motion().yieldingTo != 0;
                require(game.movementStatistics().searches <= Simulation::movementSearchesPerTick, "Passage retries exceeded budget");
            }
            if (!settled(game)) for (const auto& u : game.units()) std::cerr << "passage=" << width << " mixed=" << mixed << " ramp=" << ramp << " group=" << u.motion().moveGroup
                << " pos=" << u.position.x << ',' << u.position.y << " blocked=" << u.motion().blockedTicks << " yield=" << u.motion().yieldingTo
                << " next=" << u.motion().next << '/' << u.motion().route.size() << " radius=" << u.definition.collisionRadius
                << " waypoint=" << (u.motion().next < u.motion().route.size() ? u.motion().route[u.motion().next].x : -1) << ',' << (u.motion().next < u.motion().route.size() ? u.motion().route[u.motion().next].y : -1) << '\n';
            require(settled(game), "Opposing groups did not arrive after passage");
            if (mixed && width == 1) require(yielded, "Narrow passage did not exercise traffic arbitration");
            for (size_t i = 0; i < ids.size(); ++i) {
                const auto& u = *game.unit(ids[i]);
                require(i < 6 ? u.position.x > 28 : u.position.x < 20, "Arrival accepted on wrong side");
                require(groundLength(u.position - center(u.motion().groupTarget)) < u.motion().groupRadius + .71f, "Group did not reform");
            }
            for (const auto& a : game.units()) for (const auto& b : game.units())
                if (a.motion().moveGroup == b.motion().moveGroup && a.definition.formationPriority < b.definition.formationPriority)
                    require(groundDot(a.position - b.position, a.motion().formationForward) >= -.05f, "Roles did not reform after passage");
        }
    });
    test("Stop and Hold cancel an active retreat inside a contested corridor", [] {
        Scenario s{Map(48, 36), {1, 1}, {20, 17}, {}};
        for (int x = 17; x <= 30; ++x) for (int y = 0; y < 36; ++y) if (y != 17) s.map.at({x, y}).blocked = true;
        s.extraWorkers = {{21, 17}, {22, 17}, {25, 17}, {26, 17}, {27, 17}};
        EntityDefinition worker; worker.collisionRadius = .5f; worker.attackDamage = 0;
        auto game = makeTestSimulation(std::move(s), {}, worker);
        const auto ids = selected(game);
        game.order(std::span(ids).first(3), OrderKind::Move, {38, 17});
        game.order(std::span(ids).subspan(3), OrderKind::Move, {10, 17});
        bool yielding = false;
        for (int tick = 0; tick < 300 && !yielding; ++tick) {
            game.tick(); checkMotion(game);
            yielding = game.unit(ids[3])->motion().yieldingTo != 0;
        }
        require(yielding, "Encounter inside corridor did not initiate retreat");
        game.order(std::array{ids[3]}, OrderKind::Stop);
        game.order(std::array{ids[4]}, OrderKind::Hold);
        const auto stopped = game.unit(ids[3])->position, held = game.unit(ids[4])->position;
        for (int tick = 0; tick < 150; ++tick) {
            game.tick(); checkMotion(game);
            require(game.unit(ids[3])->position == stopped && game.unit(ids[4])->position == held, "Traffic displaced a cancelled retreat");
        }
        require(!game.unit(ids[3])->motion().yieldingTo && !game.unit(ids[4])->motion().yieldingTo, "Cancelled retreat kept a reservation");
    });
    test("Contested corridors arbitrate both directions across different owners", [] {
        for (bool transpose : {false, true}) for (bool rightFirst : {false, true}) {
            const auto cell = [&](int x, int y) { return transpose ? Cell{y, x} : Cell{x, y}; };
            Scenario s{Map(48, 48), {1, 1}, cell(20, 23), {}};
            for (int x = 17; x <= 30; ++x) for (int y = 0; y < 48; ++y)
                if (y != 23) s.map.at(cell(x, y)).blocked = true;
            EntityDefinition worker; worker.collisionRadius = .5f; worker.attackDamage = 0;
            for (int x : {21, 22}) s.units.push_back({worker.id, 0, cell(x, 23)});
            for (int x : {25, 26, 27}) s.units.push_back({worker.id, 1, cell(x, 23)});
            auto game = makeTestSimulation(std::move(s), {}, worker);
            std::array<std::vector<EntityId>, 2> armies;
            for (const auto& u : game.units()) armies[u.owner].push_back(u.id);
            for (PlayerId owner : {PlayerId(rightFirst), PlayerId(!rightFirst)})
                require(game.orderFor(owner, armies[owner], OrderKind::Move, cell(owner ? 10 : 38, 23)), "Corridor order rejected");
            bool yielded = false;
            for (int tick = 0; tick < 1800 && !settled(game); ++tick) {
                game.tick(); checkMotion(game);
                for (const auto& u : game.units()) if (u.motion().yieldingTo) {
                    yielded = true;
                    require(u.owner != PlayerId(rightFirst), "Earlier order gave up right of way");
                }
            }
            require(yielded, "Encounter did not exercise traffic arbitration");
            require(settled(game), "A corridor direction did not finish retreating and arriving");
            for (const auto& u : game.units()) {
                const float position = transpose ? u.position.y : u.position.x;
                require(u.owner ? position < 17 : position > 30, "Group stopped before crossing the corridor");
            }
        }
    });
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
                for (const auto& u : game.units()) distance = std::max(distance, groundLength(u.motion().routeDestination - u.position));
                const int deadline = int(std::ceil(distance / siege.movementPerSecond * Simulation::ticksPerSecond)) + 400;
                for (int tick = 0; tick < deadline && !settled(game); ++tick) { game.tick(); checkMotion(game); }
                if (!settled(game)) for (const auto& u : game.units()) std::cerr << "mixed " << direction.x << ',' << direction.y << " sign=" << sign
                    << " role=" << u.definition.formationPriority << " pos=" << u.position.x << ',' << u.position.y
                    << " goal=" << u.motion().routeDestination.x << ',' << u.motion().routeDestination.y << " state=" << int(u.orders().state)
                    << " blocked=" << u.motion().blockedTicks << " next=" << u.motion().next << '/' << u.motion().route.size() << '\n';
                require(settled(game), "Mixed group did not finish reforming");
                for (const auto& a : game.units()) for (const auto& b : game.units()) if (a.definition.formationPriority < b.definition.formationPriority) {
                    const Vec2 delta = a.position - b.position;
                    require(groundDot(delta, a.motion().formationForward) >= -.05f, "Rear role finished ahead of front role");
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
                    distances[i] += std::sqrt(groundLengthSquared(u.position - u.motion().tickPosition));
                    if (u.orders().state == UnitState::Idle && first < 0) first = tick;
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
                if (u.motion().next >= u.motion().route.size()) continue; // Arrival has an explicit final group heading.
                const auto delta = u.position - u.motion().tickPosition;
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
            if (open) require(game.worker().cell == Cell{17, 10} && game.worker().orders().state == UnitState::Idle, "Unit failed to escape through open gap");
        }
    });
}
}
