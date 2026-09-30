#include "TestSupport.hpp"

namespace rts::tests {
void formationTests(TestSuite& test, const TestContext& context) {
    (void)context;
    test("Group move uses distinct destinations without overlapping unit cells", [] {
        auto s = flatScenario(); s.extraWorkers = {{5, 3}, {4, 4}, {5, 4}};
        rts::Simulation game(std::move(s));
        std::vector<rts::EntityId> ids;
        for (const auto& u : game.units()) ids.push_back(u.id);
        require(game.command(ids, {7, 5}), "Group order rejected");
        for (int t = 0; t < 900; ++t) {
            game.tick();
            for (size_t i = 0; i < game.units().size(); ++i) for (size_t j = i + 1; j < game.units().size(); ++j)
                require(game.units()[i].cell != game.units()[j].cell, "Units share occupied cell");
        }
        for (const auto& u : game.units()) {
            require(u.state == rts::UnitState::Idle, "Group did not settle");
            require(std::abs(u.cell.x - 7) <= 1 && std::abs(u.cell.y - 5) <= 1, "Unit failed to reach formation");
        }
    });
    test("Two-wide formation is stable, centred and ordered by unit priority", [] {
        rts::Map map(64, 64);
        std::vector<rts::FormationMember> members;
        for (int i = 0; i < 12; ++i) members.push_back({rts::EntityId(20 - i), {8 + i % 2, 20 + i / 2}});
        const auto forward = rts::planFormation(map, members, {40, 24}, {});
        std::reverse(members.begin(), members.end());
        const auto reverse = rts::planFormation(map, members, {40, 24}, {});
        require(forward.size() == 12 && reverse.size() == 12, "Formation dropped members");
        rts::Vec2 mean{};
        for (size_t i = 0; i < forward.size(); ++i) {
            require(forward[i].id == reverse[i].id && forward[i].cell == reverse[i].cell, "Selection order changed destinations");
            mean = mean + rts::Vec2{float(forward[i].cell.x), float(forward[i].cell.y)};
            auto member = std::find_if(members.begin(), members.end(), [&](const auto& m) { return m.id == forward[i].id; });
            member->start = forward[i].cell; member->forward = forward[i].forward;
        }
        mean = mean * (1.0f / 12);
        require(std::abs(mean.x - 40) <= .5f && std::abs(mean.y - 24) <= .5f, "Formation centre missed closest grid alignment");
        const auto repeat = rts::planFormation(map, members, {40, 24}, {});
        for (size_t i = 0; i < forward.size(); ++i)
            require(forward[i].id == repeat[i].id && forward[i].cell == repeat[i].cell, "Repeated order shifted settled formation");

        members.clear();
        for (int i = 0; i < 12; ++i) members.push_back({rts::EntityId(i + 1), {10, 10 + i}, rts::MovementType::Walking, i < 6 ? 1 : 2});
        const auto ranked = rts::planFormation(map, members, {40, 16}, {});
        float lastFront = 1000, firstBack = -1000;
        for (const auto& d : ranked) {
            const float depth = d.cell.x * d.forward.x + d.cell.y * d.forward.y;
            if (d.id <= 6) lastFront = std::min(lastFront, depth);
            else firstBack = std::max(firstBack, depth);
        }
        require(lastFront > firstBack, "Ranged priority entered front ranks");
    });
    test("Obstacle-constrained formation uses reachable unique places with stable matching", [] {
        rts::Map map(20, 20);
        for (int y = 0; y < 20; ++y) map.at({12, y}).blocked = true;
        map.at({9, 10}).blocked = true;
        std::vector<rts::FormationMember> members{{1, {3, 5}}, {2, {4, 5}}, {3, {3, 6}}, {4, {4, 6}}};
        const std::array<rts::FormationObstacle, 1> held{{{{10, 9}, false}}};
        const auto result = rts::planFormation(map, members, {11, 10}, held);
        require(result.size() == 4, "Lost units beside obstacle");
        std::reverse(members.begin(), members.end());
        const auto reversed = rts::planFormation(map, members, {11, 10}, held);
        for (size_t i = 0; i < result.size(); ++i) {
            require(result[i].id == reversed[i].id && result[i].cell == reversed[i].cell, "Matching depends on selection order");
            require(result[i].cell != held[0].cell && result[i].cell.x < 12 && map.walkable(result[i].cell), "Invalid formation place");
            for (size_t j = i + 1; j < result.size(); ++j) require(result[i].cell != result[j].cell, "Duplicate places");
        }
    });
    test("Dense group moves in eight directions without initial backtracking or overlap", [] {
        for (rts::Cell direction : {rts::Cell{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}) {
            rts::Scenario s{rts::Map(32, 32), {28, 28}, {10, 10}, {}};

            s.extraWorkers = {{11, 10}, {12, 10}, {10, 11}, {11, 11}, {12, 11}};
            rts::Simulation game(std::move(s));
            std::vector<rts::EntityId> ids;
            std::vector<rts::Vec2> previous;
            for (const auto& u : game.units()) { ids.push_back(u.id); previous.push_back(u.position); }
            std::reverse(ids.begin(), ids.end());
            const rts::Cell target{11 + direction.x * 7, 10 + direction.y * 7};
            require(game.command(ids, target), "Group order failed");
            for (int tick = 0; tick < 600; ++tick) {
                game.tick();
                for (size_t i = 0; i < game.units().size(); ++i) {
                    const auto& u = game.units()[i];
                    const auto delta = u.position - previous[i];
                    if (delta.x * direction.x + delta.y * direction.y < -.001f) {
                        std::cerr << "backtrack direction=" << direction.x << ',' << direction.y << " tick=" << tick << " id=" << u.id
                            << " cell=" << u.cell.x << ',' << u.cell.y << " next=" << u.route[u.next].x << ',' << u.route[u.next].y << '\n';
                        for (const auto& other : game.units()) std::cerr << "  " << other.id << " at " << other.cell.x << ',' << other.cell.y
                            << " goal " << (other.route.empty() ? other.cell.x : other.route.back().x) << ',' << (other.route.empty() ? other.cell.y : other.route.back().y) << '\n';
                        require(false, "Unnecessary backward motion on open ground");
                    }
                    require(std::hypot(delta.x, delta.y) < .1f, "Group movement teleported");
                    previous[i] = u.position;
                    for (size_t j = i + 1; j < game.units().size(); ++j)
                        require(u.cell != game.units()[j].cell, "Group units overlapped");
                }
            }
            for (const auto& u : game.units())
                require(u.state == rts::UnitState::Idle && std::abs(u.cell.x - target.x) <= 3 && std::abs(u.cell.y - target.y) <= 3, "Group failed to arrive together");
        }
    });
    test("Twelve-unit column has six ranks of two and preserves travel lanes", [] {
        rts::Scenario s{rts::Map(64, 64), {1, 1}, {12, 20}, {}};
        std::vector<rts::FormationMember> input;
        for (int i = 0; i < 12; ++i) input.push_back({rts::EntityId(i + 1), {6 + i % 2, 10 + i / 2}});
        const auto initial = rts::planFormation(s.map, input, {20, 20}, {});
        s.worker = initial[0].cell;
        for (size_t i = 1; i < initial.size(); ++i) s.extraWorkers.push_back(initial[i].cell);
        rts::Simulation game(std::move(s));
        std::vector<rts::EntityId> ids;
        std::vector<rts::FormationMember> members;
        for (const auto& u : game.units()) { ids.push_back(u.id); members.push_back({u.id, u.cell}); }
        const auto expected = rts::planFormation(game.map(), members, {43, 20}, {});
        require(game.command(ids, {43, 20}), "12-unit command failed");
        for (int tick = 0; tick < 1200; ++tick) {
            game.tick();
            for (size_t i = 0; i < game.units().size(); ++i) for (size_t j = i + 1; j < game.units().size(); ++j)
                require(game.units()[i].cell != game.units()[j].cell, "Column overlapped");
        }
        std::vector<int> rows;
        for (const auto& d : expected) {
            require(game.unit(d.id)->cell == d.cell && game.unit(d.id)->state == rts::UnitState::Idle, "Column failed to reach assigned slot");
            rows.push_back(d.cell.x);
        }
        std::sort(rows.begin(), rows.end());
        for (size_t i = 0; i < rows.size(); i += 2)
            require(rows[i] == rows[i + 1] && (i == 0 || rows[i] != rows[i - 1]), "Expected six ranks of two");
    });
    test("Column preserves spacing while travelling and reforms after a narrow gate", [] {
        for (bool gate : {false, true}) {
            rts::Scenario s{rts::Map(64, 48), {1, 1}, {10, 19}, {}};
            for (int row = 0; row < 6; ++row) for (int column = 0; column < 2; ++column) {
                rts::Cell c{10 + row, 19 + column};
                if (c != s.worker) s.extraWorkers.push_back(c);
            }
            if (gate) for (int y = 0; y < 48; ++y) if (y != 20) s.map.at({30, y}).blocked = true;
            rts::Simulation game(std::move(s));
            std::vector<rts::EntityId> ids;
            std::vector<rts::FormationMember> members;
            std::vector<rts::Vec2> starts;
            for (const auto& u : game.units()) { ids.push_back(u.id); members.push_back({u.id, u.cell}); starts.push_back(u.position); }
            const auto expected = rts::planFormation(game.map(), members, {46, 20}, {});
            game.command(ids, {46, 20});
            for (int tick = 0; tick < 2400; ++tick) {
                game.tick();
                if (!gate && tick < 250) {
                    const auto displacement = game.units()[0].position - starts[0];
                    for (size_t i = 1; i < game.units().size(); ++i) {
                        const auto d = game.units()[i].position - starts[i] - displacement;
                        if (std::hypot(d.x, d.y) >= .15f) {
                            std::cerr << "spacing tick=" << tick << " id=" << game.units()[i].id << " delta=" << d.x << ',' << d.y << '\n';
                            for (const auto& u : game.units()) std::cerr << u.id << " at " << u.position.x << ',' << u.position.y
                                << " next " << (u.next < u.route.size() ? u.route[u.next].x : -1) << ',' << (u.next < u.route.size() ? u.route[u.next].y : -1) << '\n';
                            require(false, "Open-ground column collapsed while moving");
                        }
                    }
                }
                for (const auto& u : game.units()) require(game.map().walkable(u.cell), "Column crossed wall");
            }
            for (const auto& d : expected)
                require(game.unit(d.id)->cell == d.cell && game.unit(d.id)->state == rts::UnitState::Idle, gate ? "Column did not reform after gate" : "Open column missed slot");
        }
    });
    test("Mixed movement formation uses reachable terrain and odd ranks stay centred", [] {
        using M = rts::MovementType; using S = rts::Surface;
        rts::Map map(40, 32);
        for (int y = 0; y < 32; ++y) for (int x = 18; x < 40; ++x) map.at({x, y}).surface = S::DeepWater;
        const std::vector<rts::FormationMember> mixed{{1, {5, 14}, M::Walking, 1}, {2, {20, 14}, M::Swimming, 1},
            {3, {5, 15}, M::Amphibious, 2}, {4, {5, 16}, M::Flying, 2}};
        const auto destinations = rts::planFormation(map, mixed, {18, 20}, {});
        require(destinations.size() == mixed.size(), "Mixed group lost a member at shoreline");
        for (const auto& d : destinations) {
            const auto& m = mixed[d.id - 1];
            require(map.walkable(d.cell, m.movement) && rts::findPath(map, m.start, d.cell, m.movement).has_value(), "Mixed group assigned unreachable terrain");
        }
        rts::Map land(64, 64);
        std::vector<rts::FormationMember> odd;
        for (int i = 0; i < 13; ++i) odd.push_back({rts::EntityId(i + 1), {5, 10 + i}});
        const auto thirteen = rts::planFormation(land, odd, {40, 16}, {});
        require(thirteen.size() == 13, "Odd formation lost last unit");
        rts::Vec2 mean{};
        for (const auto& d : thirteen) mean = mean + rts::Vec2{float(d.cell.x), float(d.cell.y)};
        mean = mean * (1.0f / 13);
        require(std::abs(mean.x - 40) <= .51f && std::abs(mean.y - 16) <= .51f, "Odd formation is not centred");
    });
    test("Twelve-unit groups reach oriented slots in all eight directions", [] {
        for (rts::Cell direction : {rts::Cell{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}) {
            rts::Scenario s{rts::Map(64, 64), {1, 1}, {30, 30}, {}};
            for (int i = 1; i < 12; ++i) s.extraWorkers.push_back({30 + i % 4, 30 + i / 4});
            rts::Simulation game(std::move(s));
            std::vector<rts::EntityId> ids; std::vector<rts::FormationMember> members;
            for (const auto& u : game.units()) { ids.push_back(u.id); members.push_back({u.id, u.cell}); }
            const rts::Cell target{31 + direction.x * 14, 31 + direction.y * 14};
            const auto expected = rts::planFormation(game.map(), members, target, {});
            game.command(ids, target);
            ticks(game, 1800);
            for (const auto& d : expected) if (game.unit(d.id)->cell != d.cell) {
                std::cerr << "12-unit miss direction=" << direction.x << ',' << direction.y << " id=" << d.id
                    << " at=" << game.unit(d.id)->cell.x << ',' << game.unit(d.id)->cell.y << " goal=" << d.cell.x << ',' << d.cell.y << '\n';
                for (const auto& u : game.units()) std::cerr << u.id << " at " << u.cell.x << ',' << u.cell.y << " blocked=" << u.blockedTicks
                    << " next " << (u.next < u.route.size() ? u.route[u.next].x : -1) << ',' << (u.next < u.route.size() ? u.route[u.next].y : -1) << '\n';
                require(false, "Twelve-unit group missed assigned slot");
            }
        }
    });
    test("Dense formation fills adjacent cells and has unique connected slots at every heading", [] {
        rts::Map map(64, 64);
        for (int heading = 0; heading < 32; ++heading) {
            const float angle = heading * 6.283185307f / 32;
            const rts::Cell start{32 - int(std::lround(std::cos(angle) * 20)), 32 - int(std::lround(std::sin(angle) * 20))};
            std::vector<rts::FormationMember> members;
            for (int i = 0; i < 12; ++i) members.push_back({rts::EntityId(i + 1), {start.x + i % 2, start.y + i / 2}});
            const auto slots = rts::planFormation(map, members, {32, 32}, {});
            require(slots.size() == 12, "Dense formation lost members");
            for (size_t i = 0; i < slots.size(); ++i) for (size_t j = i + 1; j < slots.size(); ++j)
                require(slots[i].cell != slots[j].cell, "Diagonal snapping merged two slots");
            std::vector<bool> connected(slots.size()); connected[0] = true;
            for (size_t pass = 0; pass < slots.size(); ++pass) for (size_t i = 0; i < slots.size(); ++i) if (connected[i])
                for (size_t j = 0; j < slots.size(); ++j) {
                    const auto d = slots[i].cell - slots[j].cell;
                    if (std::abs(d.x) + std::abs(d.y) == 1) connected[j] = true;
                }
            if (!std::all_of(connected.begin(), connected.end(), [](bool v) { return v; })) {
                std::cerr << "disconnected heading=" << heading << '\n';
                for (const auto& slot : slots) std::cerr << slot.cell.x << ',' << slot.cell.y << ' ';
                std::cerr << '\n';
                require(false, "Dense ranks have empty gaps between them");
            }
        }
        for (rts::Cell direction : {rts::Cell{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            std::vector<rts::FormationMember> members;
            for (int i = 0; i < 12; ++i) members.push_back({rts::EntityId(i + 1), {32 - direction.x * 20, 32 - direction.y * 20}});
            const auto slots = rts::planFormation(map, members, {32, 32}, {});
            int minX = 64, maxX = 0, minY = 64, maxY = 0;
            for (const auto& slot : slots) {
                minX = std::min(minX, slot.cell.x); maxX = std::max(maxX, slot.cell.x);
                minY = std::min(minY, slot.cell.y); maxY = std::max(maxY, slot.cell.y);
            }
            require(maxX - minX + 1 == (direction.x ? 6 : 2) && maxY - minY + 1 == (direction.x ? 2 : 6), "Twelve units must fill exactly six by two adjacent cells");
        }
    });
    test("Dense followers preserve separation when the leading unit stops or reverses", [] {
        for (bool reverse : {false, true}) {
            rts::Scenario s{rts::Map(40, 20), {1, 1}, {10, 10}, {}};
            s.extraWorkers = {{11, 10}, {12, 10}, {10, 11}, {11, 11}, {12, 11}};
            rts::Simulation game(std::move(s));
            std::vector<rts::EntityId> ids;
            for (const auto& u : game.units()) ids.push_back(u.id);
            game.command(ids, {30, 10}); ticks(game, 40);
            const auto leader = game.units()[2].id;
            if (reverse) game.command(std::span<const rts::EntityId>(&leader, 1), {8, 8});
            else game.stop(std::span<const rts::EntityId>(&leader, 1));
            for (int tick = 0; tick < 500; ++tick) {
                game.tick();
                for (size_t i = 0; i < game.units().size(); ++i) for (size_t j = i + 1; j < game.units().size(); ++j) {
                    require(game.units()[i].cell != game.units()[j].cell, "Followers entered the same cell");
                    const auto d = game.units()[i].position - game.units()[j].position;
                    require(std::hypot(d.x, d.y) >= .7f, "Followers intersected while their leader stopped or turned");
                }
            }
        }
    });
}
}
