#pragma once
#include "rts/Paths.hpp"
#include "rts/Simulation.hpp"
#include "rts/Definitions.hpp"
#include "rts/GameplayUi.hpp"
#include "rts/Formation.hpp"
#include "rts/UnitAnimation.hpp"
#include "rts/FogMask.hpp"
#include "rts/WorldEditor.hpp"
#include "rts/TerrainPaint.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace rts::tests {
inline void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline nlohmann::json mapJson(int width=6,int height=6) {
    using J=nlohmann::json;
    return {{"format","rts-world"},{"name","Тест"},{"size",{width,height}},
        {"start",{{"hall",{0,0}},{"workers",J::array({{width-1,height-1}})},{"hero",nullptr},{"crystals",0}}},
        {"terrain",{{"base","grass"},{"heights",std::vector<std::string>(height,std::string(width,'0'))},{"blocked",std::vector<std::string>(height,std::string(width,'0'))},{"surfaces",std::vector<std::string>(height,std::string(width,'L'))},{"ramps",J::array()}}},
        {"paint",J::array()},{"environment",J::array()},{"decorations",J::array()},{"resources",J::array()},{"units",J::array()}};
}
inline void writeMap(const std::filesystem::path& file,const nlohmann::json& j) { std::ofstream out(file); out<<j.dump(2); }
template<class F> void mustThrow(F action) {
    bool threw = false;
    try { action(); } catch (const std::exception&) { threw = true; }
    require(threw, "Expected an exception");
}
inline rts::Scenario flatScenario(int amount = 23) {
    rts::Scenario s{rts::Map(10, 10), {1, 1}, {4, 3}, {{{7, 7}, amount}}};
    s.map.occupy({7, 7});
    return s;
}
inline rts::Scenario gatheringScenario(bool neighbors = true) {
    rts::Scenario s{rts::Map(24, 16), {1, 1}, {4, 8}, {{{15, 8}, 1000}, {{6, 5}, 1000}}};
    if (neighbors) s.crystals.push_back({{17, 8}, 1000});
    for (const auto& crystal : s.crystals) s.map.occupy(crystal.cell);
    return s;
}
inline rts::EntityDefinition gatheringWorker() {
    rts::EntityDefinition type; type.dayVision = 32; type.nightVision = 32;
    return type;
}
inline rts::Scenario shoreScenario() {
    rts::Scenario s{rts::Map(32, 24), {1, 1}, {4, 10}, {}};
    for (int y = 0; y < 24; ++y) for (int x = 12; x <= 19; ++x) {
        auto& tile = s.map.at({x, y});
        tile.height = -1;
        tile.surface = (y == 10 || y == 11 || x <= 13 || x >= 18) ? rts::Surface::ShallowWater : rts::Surface::DeepWater;
    }
    for (int y : {10, 11}) {
        s.map.at({11, y}).height = -1; s.map.at({11, y}).ramp = {-1, 0};
        s.map.at({20, y}).height = -1; s.map.at({20, y}).ramp = {1, 0};
    }
    return s;
}
inline bool separated(const rts::Unit& a, const rts::Unit& b) {
    const auto delta = a.position - b.position;
    return std::hypot(delta.x, delta.y) + .00001f >= a.definition.collisionRadius + b.definition.collisionRadius;
}
inline void ticks(rts::Simulation& game, int count) { for (int i = 0; i < count; ++i) game.tick(); }

using Json = nlohmann::json;
struct CatalogFixture {
    std::filesystem::path root;
    Json catalog, entities, commanders, rules;
    explicit CatalogFixture(const std::filesystem::path& assets) {
        const auto read = [](const std::filesystem::path& path) { std::ifstream file(path); return Json::parse(file); };
        root = rts::Paths::executable().parent_path() / L"Каталог для проверки";
        catalog = read(assets / "data/catalog.json");
        entities = read(assets / "data/entities/humans.json");
        commanders = read(assets / "data/commanders.json");
        rules = read(assets / "data/rules.json");
    }
    rts::Definitions load() const {
        std::filesystem::create_directories(root / "entities");
        std::ofstream(root / "catalog.json") << catalog.dump(2);
        std::ofstream(root / "entities/humans.json") << entities.dump(2);
        std::ofstream(root / "commanders.json") << commanders.dump(2);
        std::ofstream(root / "rules.json") << rules.dump(2);
        return rts::Definitions::load(root / "catalog.json");
    }
};
inline rts::EntityDefinition testDepot(const std::string& id, const std::string& produced, int ticks = 1) {
    rts::EntityDefinition depot;
    depot.id = id; depot.displayName = id; depot.constructible = true; depot.mobile = false;
    depot.canBuild = false; depot.carryCapacity = 0; depot.width = 3; depot.height = 2;
    depot.cost = {100, 0}; depot.constructionTicks = ticks; depot.maximumHealth = 200;
    depot.acceptsCargo = true; depot.trainableUnits = {produced}; depot.visual = rts::EntityVisual::Hall;
    return depot;
}

inline int oracleCost(const rts::Map& map, rts::Cell start, rts::Cell goal) {
    using Item = std::pair<int, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
    std::vector<int> costs(static_cast<size_t>(map.width() * map.height()), 1000000);
    const auto index = [&](rts::Cell c) { return c.y * map.width() + c.x; };
    costs[index(start)] = 0;
    queue.push({0, index(start)});
    while (!queue.empty()) {
        const auto [cost, id] = queue.top(); queue.pop();
        if (cost != costs[id]) continue;
        const rts::Cell c{id % map.width(), id / map.width()};
        if (c == goal) return cost;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const rts::Cell next{c.x + dx, c.y + dy};
            // Independent flat-map oracle, including two-sided corner prevention.
            if ((!dx && !dy) || !map.walkable(next)) continue;
            if (dx && dy && (!map.walkable({c.x + dx, c.y}) || !map.walkable({c.x, c.y + dy}))) continue;
            const int candidate = cost + ((dx && dy) ? 14 : 10);
            if (candidate < costs[index(next)]) { costs[index(next)] = candidate; queue.push({candidate, index(next)}); }
        }
    }
    return -1;
}

struct TestSuite {
    int passed{}, failures{};
    void operator()(const char* name, const std::function<void()>& action) {
        try { action(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
};
struct TestContext {
    std::filesystem::path assets;
    rts::Paths worldPaths;
    rts::WorldAssets worldAssets;
    rts::Cell hallFootprint;
    explicit TestContext(const std::filesystem::path& assetRoot) : assets(assetRoot),
        worldPaths(assets, rts::Paths::executable().parent_path() / L"world-tests"),
        worldAssets(rts::WorldAssets::load(worldPaths)) {
        const auto definitions = rts::Definitions::load(assets / "data/catalog.json");
        const auto& depot = definitions.startingDepot(definitions.commanders().front().factionId);
        hallFootprint = {depot.width, depot.height};
    }
};
void navigationTests(TestSuite& test, const TestContext& context);
void economyTests(TestSuite& test, const TestContext& context);
void combatTests(TestSuite& test, const TestContext& context);
void projectileTests(TestSuite& test, const TestContext& context);
void dataTests(TestSuite& test, const TestContext& context);
void spriteTests(TestSuite& test, const TestContext& context);
void visionTests(TestSuite& test, const TestContext& context);
void lightingTests(TestSuite& test, const TestContext& context);
void selectionTests(TestSuite& test, const TestContext& context);
void cameraTests(TestSuite& test);
void formationTests(TestSuite& test, const TestContext& context);
void movementTests(TestSuite& test, const TestContext& context);
void editorTests(TestSuite& test, const TestContext& context);
}
