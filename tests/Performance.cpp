#include "rts/FogOfWar.hpp"
#include "rts/Navigation.hpp"
#include "rts/Formation.hpp"
#include "rts/Simulation.hpp"
#include "rts/WorldEditor.hpp"
#include "rts/FogMask.hpp"
#include "rts/TerrainVisibility.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
template<class Work> void measure(const char* name, Work work) {
    using Clock = std::chrono::steady_clock;
    std::vector<double> samples;
    std::uint64_t checksum = 0;
    work(); // Warm caches; report the median, not process startup or the first run.
    for (int repeat = 0; repeat < 7; ++repeat) {
        const auto start = Clock::now();
        checksum += work();
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    std::cout << name << ": " << std::fixed << std::setprecision(3) << samples[3]
              << " ms; checksum=" << checksum << '\n';
}
void presentationBenchmark() {
    for (int size : {128, 512}) {
        rts::Map map(size, size);
        rts::FogOfWar first(size, size), second(size, size);
        const rts::VisionSource a{{size / 2, size / 2}, 10}, b{{size / 2 + 1, size / 2}, 10};
        first.update(map, std::span(&a, 1));
        first.update(map, std::span(&b, 1));
        second = first;
        second.update(map, std::span(&a, 1));
        rts::FogMask mask;
        mask.update(first, size, size);
        const auto name = "fog mask: " + std::to_string(size) + "x" + std::to_string(size) + " / 8 local observer changes";
        measure(name.c_str(), [&] {
            for (int i = 0; i < 8; ++i) mask.update(i % 2 ? first : second, size, size);
            std::uint64_t checksum = 0;
            for (auto pixel : mask.pixels()) checksum += pixel;
            return checksum;
        });
    }
    rts::Map terrain(512, 512);
    for (int y = 0; y < 512; ++y) for (int x = 0; x < 512; ++x) terrain.at({x, y}).height = (x + y) % 5 - 1;
    for (bool clipped : {false, true}) {
        measure(clipped ? "terrain candidates: camera-bounded / 512x512 / 16 views" :
            "terrain candidates: full scan / 512x512 / 16 views", [&] {
            std::uint64_t checksum = 0;
            for (int frame = 0; frame < 16; ++frame) {
                const rts::WorldView view{{720.f + frame * 13, 450 - 512 * 32 * .85f}, .85f};
                for (int depth = 0; depth < 1024; ++depth) {
                    const auto columns = clipped ? rts::terrainColumns(view, 512, 512, depth, -250, 1690) :
                        rts::TerrainColumns{std::max(0, depth - 511), std::min(512, depth + 1)};
                    for (int x = columns.begin; x < columns.end; ++x) {
                        const rts::Cell cell{x, depth - x};
                        const auto p = view.project(rts::center(cell), float(terrain.at(cell).height));
                        if (p.x > -250 && p.x < 1690 && p.y > -100 && p.y < 1080)
                            checksum += 1 + cell.x + cell.y * 512;
                    }
                }
            }
            return checksum;
        });
    }
}
void combatBenchmark() {
    using Clock = std::chrono::steady_clock;
    for (int count : {24, 50}) for (bool focus : {false, true}) {
        rts::Scenario site{rts::Map(96, 96), {1, 1}, {30, 40}, {}};
        rts::EntityDefinition fighter;
        fighter.attackDamage = 10; fighter.maximumHealth = 100000; fighter.collisionRadius = .45f;
        fighter.canBuild = false; fighter.dayVision = fighter.nightVision = 30;
        for (int i = 0; i < count; ++i) {
            if (i) site.units.push_back({fighter.id, 0, {30 + i % 5, 40 + i / 5}});
            site.units.push_back({fighter.id, 1, {42 + i % 5, 40 + i / 5}});
        }
        rts::Simulation game(std::move(site), {}, fighter, {fighter});
        std::vector<rts::EntityId> own;
        rts::EntityId target{};
        for (const auto& u : game.units()) {
            if (u.owner == 0) own.push_back(u.id); else if (!target) target = u.id;
        }
        const auto commandStart = Clock::now();
        if (focus) game.attack(own, target);
        const double commandMs = std::chrono::duration<double, std::milli>(Clock::now() - commandStart).count();
        std::vector<double> samples;
        for (int tick = 0; tick < 300; ++tick) {
            const auto start = Clock::now(); game.tick();
            samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        }
        const double first = samples.front();
        std::sort(samples.begin(), samples.end());
        int damage = 0, engaged = 0;
        for (const auto& u : game.units()) {
            damage += u.maximumHealth() - u.health;
            engaged += u.state == rts::UnitState::Attacking;
        }
        std::cout << "combat " << count << "v" << count << (focus ? " focus" : " automatic")
                  << " / 96x96 / 300 ticks: command=" << commandMs << " ms; first=" << first
                  << " ms; p50=" << samples[150] << " ms; p95=" << samples[285]
                  << " ms; max=" << samples.back() << " ms; damage=" << damage << "; engaged=" << engaged << std::endl;
    }
}
void editorBenchmark() {
    const auto paths = rts::Paths::discover();
    const auto assets = rts::WorldAssets::load(paths);
    const auto definitions = rts::Definitions::load(paths.asset(L"data/catalog.json"));
    rts::Scenario scenario{rts::Map(256, 256), {1, 1}, {4, 3}, {}};
    for (int y = 20; y < 240; y += 8) for (int x = 20; x < 240; x += 8)
        scenario.environment.push_back(assets.instantiate("tree", 1000 + static_cast<rts::EntityId>(scenario.environment.size()), {x, y}));
    scenario.landscape.paint.resize(2000, {"earth", {8.5f, 8.5f}, 1.5f, .65f, .4f, false});
    rts::WorldEditor editor(std::move(scenario), assets, definitions);
    editor.radius = .75f;
    measure("editor: 256x256 / 784 trees / 2000 stamps / stroke+undo+redo", [&] {
        editor.beginStroke();
        editor.apply({8.5f, 9.5f}); editor.apply({72.5f, 9.5f}); editor.endStroke();
        const auto count = editor.scenario().landscape.paint.size();
        const bool undone = editor.undo(), redone = editor.redo();
        editor.undo(); // Each sample starts with the same authored document.
        return static_cast<std::uint64_t>(count * (undone && redone));
    });
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--presentation-only") { presentationBenchmark(); return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--combat-only") { combatBenchmark(); return 0; }
    combatBenchmark();
    rts::Map open(128, 128);
    std::vector<rts::FormationMember> members;
    for (int i = 0; i < 48; ++i) members.push_back({rts::EntityId(i + 1), {10 + i % 8, 20 + i / 8}});
    measure("formation: 48 units / 128x128 / one order", [&] {
        const auto destinations = rts::planFormation(open, members, {100, 64}, {});
        std::uint64_t checksum = 0;
        for (const auto& d : destinations) checksum += d.id * (d.cell.x + d.cell.y * 128);
        return checksum;
    });
    using Clock = std::chrono::steady_clock;
    const auto crowdStart = Clock::now();
    for (rts::Cell direction : {rts::Cell{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}) {
        rts::Scenario s{rts::Map(64, 64), {1, 1}, {30, 30}, {}};
        for (int i = 1; i < 12; ++i) s.extraWorkers.push_back({30 + i % 4, 30 + i / 4});
        rts::Simulation game(std::move(s));
        std::vector<rts::EntityId> ids;
        for (const auto& u : game.units()) ids.push_back(u.id);
        game.command(ids, {31 + direction.x * 14, 31 + direction.y * 14});
        int tick = 0, first = -1;
        double distance = 0;
        for (; tick < 1800; ++tick) {
            game.tick();
            int arrived = 0;
            for (const auto& u : game.units()) {
                distance += std::sqrt(rts::groundLengthSquared(u.position - u.tickPosition));
                if (u.state == rts::UnitState::Idle) ++arrived;
            }
            if (arrived && first < 0) first = tick;
            if (arrived == 12) break;
        }
        if (tick == 1800) for (const auto& u : game.units())
            std::cout << "  unit " << u.id << " at " << u.position.x << ',' << u.position.y << " state=" << int(u.state)
                      << " blocked=" << u.blockedTicks << " next=" << u.next << '/' << u.route.size()
                      << " goal=" << u.routeDestination.x << ',' << u.routeDestination.y << '\n';
        std::cout << "crowd 12 direction " << direction.x << ',' << direction.y << ": settle=" << tick
                  << " ticks; arrival spread=" << tick - first << " ticks; travel=" << distance << " cells\n";
    }
    std::cout << "crowd: eight 12-unit orders including simulation/fog: "
              << std::chrono::duration<double, std::milli>(Clock::now() - crowdStart).count() << " ms\n";

    for (int count : {48, 100}) {
        rts::Scenario s{rts::Map(96, 96), {1, 1}, {20, 20}, {}};
        for (int i = 1; i < count; ++i) s.extraWorkers.push_back({20 + i % 10, 20 + i / 10});
        rts::Simulation game(std::move(s));
        std::vector<rts::EntityId> ids;
        for (const auto& u : game.units()) ids.push_back(u.id);
        const auto commandStart = Clock::now();
        game.command(ids, {65, 60});
        const auto commandTime = std::chrono::duration<double, std::milli>(Clock::now() - commandStart).count();
        const auto motionStart = Clock::now();
        int tick = 0, first = -1, idle = 0;
        for (; tick < 1200; ++tick) {
            game.tick(); idle = 0;
            for (const auto& u : game.units()) if (u.state == rts::UnitState::Idle) ++idle;
            if (idle && first < 0) first = tick;
            if (idle == count) break;
        }
        std::cout << "crowd " << count << " on 96x96: command=" << commandTime << " ms; simulation="
                  << std::chrono::duration<double, std::milli>(Clock::now() - motionStart).count() << " ms; settled=" << idle
                  << "; ticks=" << tick << "; arrival spread=" << tick - first << '\n';
        for (const auto& u : game.units()) if (u.state != rts::UnitState::Idle)
            std::cout << "  moving " << u.id << " pos=" << u.position.x << ',' << u.position.y << " blocked=" << u.blockedTicks << '\n';
    }
    {
        rts::Scenario s{rts::Map(64, 48), {1, 1}, {10, 19}, {}};
        for (int i = 1; i < 12; ++i) s.extraWorkers.push_back({10 + i % 4, 19 + i / 4});
        for (int y = 0; y < 48; ++y) if (y != 20) s.map.at({30, y}).blocked = true;
        rts::Simulation game(std::move(s));
        std::vector<rts::EntityId> ids;
        for (const auto& u : game.units()) ids.push_back(u.id);
        game.command(ids, {46, 20});
        int tick = 0;
        for (; tick < 900; ++tick) {
            game.tick();
            if (std::all_of(game.units().begin(), game.units().end(), [](const auto& u) { return u.state == rts::UnitState::Idle; })) break;
        }
        std::cout << "crowd 12 one-cell gate: " << tick << " ticks\n";
        if (tick == 900) for (const auto& u : game.units())
            std::cout << "  gate unit " << u.id << " pos=" << u.position.x << ',' << u.position.y << " blocked=" << u.blockedTicks
                      << " state=" << int(u.state) << " next=" << u.next << '/' << u.route.size()
                      << " goal=" << u.routeDestination.x << ',' << u.routeDestination.y << '\n';
    }
    rts::Map map(128, 128);
    std::vector<rts::EnvironmentObject> forest;
    for (int y = 20; y < 110; y += 3)
        forest.push_back(rts::makeEnvironment("TREE", static_cast<rts::EntityId>(1 + forest.size()), {67, y}));
    map.rebuildVisionBlockers(forest);
    for (bool clustered : {true, false}) {
        std::vector<rts::VisionSource> sources;
        for (int i = 0; i < 48; ++i)
            sources.push_back({clustered ? rts::Cell{54 + i % 8, 50 + i / 8} : rts::Cell{8 + i % 8 * 16, 8 + i / 8 * 19}, 10, i % 8 == 0});
        rts::FogOfWar fog(map.width(), map.height());
        measure(clustered ? "fog: clustered 48 observers / 80 updates" : "fog: spread 48 observers / 80 updates", [&] {
            for (int i = 0; i < 80; ++i) fog.update(map, sources);
            std::uint64_t checksum = 0;
            for (int y = 0; y < map.height(); ++y) for (int x = 0; x < map.width(); ++x)
                checksum += (1 + x + y * map.width()) * static_cast<unsigned>(fog.at({x, y}));
            return checksum;
        });
    }
    for (int x = 12; x < 118; x += 12)
        for (int y = 4; y < 124; ++y) if (y / 8 % 3 != x / 12 % 3) map.at({x, y}).blocked = true;
    measure("navigation: 64 routes on 128x128", [&] {
        std::uint64_t checksum = 0;
        for (int i = 0; i < 64; ++i) {
            const auto path = rts::findPath(map, {2, 2 + i}, {125, 125 - i});
            if (!path) continue;
            checksum += path->cost;
            for (auto cell : path->cells) checksum = checksum * 31 + cell.x + cell.y * 128;
        }
        return checksum;
    });
    rts::Map large(512, 512);
    measure("navigation: 512 short routes on 512x512 / limit 192", [&] {
        std::uint64_t checksum = 0;
        for (int i = 0; i < 512; ++i) {
            const float y = 2.5f + i % 500;
            const auto path = rts::findUnitPathTo(large, {2.5f, y}, {10.5f, y}, {}, .35f, rts::MovementType::Walking, 192);
            if (path) checksum += path->cost + path->cells.size();
        }
        return checksum;
    });
    editorBenchmark();
    presentationBenchmark();
}
