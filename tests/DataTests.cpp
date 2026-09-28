#include "TestSupport.hpp"

namespace rts::tests {
void dataTests(TestSuite& test, const TestContext& context) {
    const auto& assets=context.assets;
    const auto loadScenario = [&](const std::filesystem::path& file) { return rts::loadScenario(file, context.worldAssets); };
    test("Large demo: workers, friendly army, hostile camp and reachable plateaus", [&] {
        const auto definitions = rts::Definitions::load(assets / "data/catalog.json");
        rts::Simulation game(loadScenario(assets / "maps/demo.rtsmap"), {}, definitions.entity("human.worker"), definitions.entities());
        require(game.map().width() == 64 && game.map().height() == 64 && game.units().size() == 28, "Wrong demo dimensions or army");
        require(game.storedCrystals() == 300 && game.armySupply().used() == 42, "Enemy army consumed player supply");
        int friendly = 0, hostile = 0;
        for (const auto& u : game.units()) if (u.definition.attackDamage > 0) {
            if (u.owner == game.player().id) ++friendly;
            else {
                ++hostile;
                require(u.cell.x >= 50 && u.cell.y >= 50 && !game.fog().visible(u.cell), "Enemy camp not in unexplored southeast");
                require(rts::findPath(game.map(), game.worker().cell, u.cell).has_value(), "Enemy camp unreachable");
            }
        }
        require(friendly == 16 && hostile == 6, "Missing starting soldiers");
        for (int y = 0; y < game.map().height(); ++y) for (int x = 0; x < game.map().width(); ++x) {
            const auto& tile = game.map().at({x, y});
            if (tile.surface != rts::Surface::Land) require(tile.height == -1 && tile.ramp == rts::Cell{}, "Water plane must be flat at -1 regardless of depth");
            if (tile.ramp != rts::Cell{}) {
                const rts::Cell across{-tile.ramp.y, tile.ramp.x};
                const auto lane = [&](rts::Cell c) {
                    return game.map().contains(c) && game.map().at(c).height == tile.height && game.map().at(c).ramp == tile.ramp &&
                        game.map().walkable(c) && game.map().walkable(c - tile.ramp) && game.map().walkable(c + tile.ramp);
                };
                int width = 1;
                for (auto c = rts::Cell{x, y} + across; lane(c); c = c + across) ++width;
                for (auto c = rts::Cell{x, y} - across; lane(c); c = c - across) ++width;
                require(width == (tile.height == -1 ? 2 : 3), "Demo ramp narrower than intended or obstructed");
            }
        }
        for (const auto endpoints : {std::pair{rts::Cell{9, 16}, rts::Cell{7, 16}}, std::pair{rts::Cell{34, 30}, rts::Cell{54, 30}}}) {
            const auto path = rts::findPath(game.map(), endpoints.first, endpoints.second);
            require(path && std::any_of(path->cells.begin(), path->cells.end(), [&](rts::Cell c) { return game.map().at(c).ramp != rts::Cell{}; }), "Demo water entry/ford has no usable shore descent");
        }
        for (rts::Cell goal : {rts::Cell{24, 14}, {29, 14}, {49, 45}})
            require(rts::findPath(game.map(), game.worker().cell, goal).has_value(), "Demo plateau inaccessible");
        require(!game.fog().explored({50, 50}), "Entire map revealed at start");
    });
    test("Unicode and spaces, traversal guards, absolute paths and alternate data streams", [&] {
        namespace fs = std::filesystem;
        const auto temp = rts::Paths::executable().parent_path() / L"test data - путь";
        const rts::Paths paths(assets, temp);
        require(fs::is_regular_file(paths.asset(L"maps/demo.rtsmap")), "Assets not resolved");
        const auto save = paths.writable(L"saves/тест сохранения.txt");
        { std::ofstream file(save); file << "unicode-path-ok"; }
        std::ifstream file(save);
        std::string contents; file >> contents;
        require(contents == "unicode-path-ok", "Unicode path failed");
        mustThrow([&] { paths.asset(L"../README.md"); });
        mustThrow([&] { paths.asset(L"C:\\Windows\\win.ini"); });
        mustThrow([&] { paths.asset(L"C:relative.txt"); });
        mustThrow([&] { paths.asset(L"\\maps\\demo.rtsmap"); });
        mustThrow([&] { paths.writable(L"saves/file:stream"); });
        mustThrow([&] { paths.writable(L"saves/NUL.txt"); });
        mustThrow([&] { paths.writable(L"saves/COM1"); });
        mustThrow([&] { paths.writable(L"saves/.. /outside.txt"); });
        mustThrow([&] { paths.asset(L"missing.png"); });
    });
    test("Malformed scenarios fail before simulation", [&] {
        const auto file = rts::Paths::executable().parent_path() / L"invalid.rtsmap";
        auto j=mapJson(); j["start"]["hall"]={5,5}; writeMap(file,j);
        mustThrow([&] { loadScenario(file); });
        j=mapJson(); j["size"]={999999,2}; writeMap(file,j);
        mustThrow([&] { loadScenario(file); });
        j=mapJson(); j["terrain"]["ramps"]={{3,3,1,0}}; writeMap(file,j);
        mustThrow([&] { loadScenario(file); });
    });
    test("Army supply is independent of crystals and cannot exceed 100", [] {
        rts::ArmySupply supply;
        require(supply.reserve(100), "Cannot fill army cap");
        require(!supply.reserve(1) && supply.used() == 100, "Exceeded hard cap");
        require(!supply.reserve(-1), "Negative army cost accepted");
        supply.release(15);
        require(supply.reserve(15) && supply.used() == 100, "Released supply was not reusable");
        mustThrow([&] { supply.release(101); });
        rts::Simulation game(flatScenario());
        require(game.armySupply().used() == 1 && game.storedCrystals() == 0, "Initial worker supply");
    });
    test("Commander catalog accepts multiple commanders and resolves shared definitions", [&] {
        CatalogFixture fixture(assets);
        auto second = fixture.commanders["commanders"][0];
        second["id"] = "two"; second["name"] = "Second";
        fixture.commanders["commanders"].push_back(second);
        const auto defs = fixture.load();
        require(defs.commanders().size() == 2 && defs.commander("two").factionId == "humans", "Commanders hardcoded");
        const rts::PlayerSettings player{0, "two", rts::TeamColor::Purple};
        rts::Simulation game(flatScenario(), player, defs.entity(defs.commander("two").startingWorker));
        require(game.player().commanderId == "two" && game.worker().owner == 0, "Ownership lost");
        fixture.commanders["commanders"][1]["startingWorker"] = "missing";
        mustThrow([&] { fixture.load(); });
        fixture.commanders["commanders"][1]["startingWorker"] = "human.soldier";
        mustThrow([&] { fixture.load(); });
    });
    test("Crystal maximum is enforced in map files and matches", [&] {
        require(rts::Crystal{}.remaining == 1000, "Default deposit is not full");
        const auto file = rts::Paths::executable().parent_path() / L"test-crystal-reserve.rtsmap";
        for (int amount : {-1, 0, 1, 1000, 1001}) {
            auto j=mapJson(); j["resources"]={{{"cell",{3,3}},{"remaining",amount}}}; writeMap(file,j);
            if (amount > 0 && amount <= 1000)
                require(loadScenario(file).crystals[0].remaining == amount, "Valid resource reserve was not loaded");
            else mustThrow([&] { loadScenario(file); });
        }
        mustThrow([] { rts::Simulation game(flatScenario(1001)); });
        mustThrow([] { rts::Simulation game(flatScenario(-1)); });
        const auto demo = loadScenario(assets / "maps/demo.rtsmap");
        require(!demo.crystals.empty() && std::all_of(demo.crystals.begin(), demo.crystals.end(), [](const auto& c) { return c.remaining == 1000; }), "Demo deposits are not full");
    });
    test("One entity schema includes names, descriptions, factions, costs and external progression", [&] {
        CatalogFixture fixture(assets); const auto defs = fixture.load();
        require(defs.entities().size() == 9, "Unified catalog lost records");
        for (const auto& entity : defs.entities()) {
            require(!entity.displayName.empty() && !entity.description.empty() && entity.factionId == "humans", "Missing common metadata");
        }
        require(defs.entity("human.hero").hero && defs.entity("human.hero").maximumHealth == 400 && defs.entity("human.hero").cost.supply == 5, "Hero common properties missing");
        require(defs.entity("human.hall").constructible && !defs.entity("human.hall").mobile && defs.entity("human.hall").cost.crystals == 200, "Building not in common catalog");
        require(defs.progression().thresholds.size() == 10, "External progression lost");
        fixture.rules["hero"]["thresholds"] = {0, 10, 30};
        fixture.rules["hero"]["experiencePerVictimLevel"] = 7;
        const auto changed = fixture.load();
        auto scenario = flatScenario(); scenario.heroSpawn = rts::Cell{5, 5};
        rts::Simulation game(std::move(scenario), {}, changed.entity("human.worker"), changed.entities(), "human.hero", changed.progression());
        game.grantExperience(game.hero()->id, 30);
        require(game.hero()->level() == 3 && game.hero()->atMaxLevel(), "Match ignored external experience rules");
    });
    test("Mobility, construction, flight and alternate forms are independent of race", [&] {
        CatalogFixture fixture(assets);
        fixture.catalog["factions"].push_back({{"id", "forest"}, {"name", "Дети леса"}, {"description", "Test faction"}});
        auto rooted = fixture.entities["entities"][3];
        rooted["id"] = "forest.rooted"; rooted["factionId"] = "forest"; rooted["name"] = "Древень";
        rooted["depot"] = false; rooted["production"]["trains"] = Json::array();
        rooted["alternateForms"] = {"forest.floating"};
        auto mobile = rooted; mobile["id"] = "forest.floating";
        mobile["mobility"]["enabled"] = true; mobile["mobility"]["type"] = "flying"; mobile["mobility"]["speed"] = 2.0;
        mobile["alternateForms"] = {"forest.rooted"};
        fixture.entities["entities"].push_back(rooted); fixture.entities["entities"].push_back(mobile);
        const auto defs = fixture.load(); const auto& type = defs.entity("forest.floating");
        require(type.constructible && type.mobile && type.movement == rts::MovementType::Flying && type.factionId == "forest", "Capabilities became exclusive kinds");
        require(type.alternateForms.front() == "forest.rooted", "Form reference lost");
        rts::Simulation game(flatScenario(), {}, defs.entity("human.worker"), defs.entities());
        require(game.buildingTypes().size() == 3 && !game.canPlace("forest.rooted", {5, 5}), "Another faction's building exposed");
        fixture.entities["entities"].back()["alternateForms"] = {"missing"};
        mustThrow([&] { fixture.load(); });
    });
    test("Catalog validates metadata, resource costs, movement, weapons and global IDs", [&] {
        const CatalogFixture original(assets);
        const auto rejects = [&](const std::function<void(Json&)>& change) {
            auto fixture = original; change(fixture.entities["entities"][0]); mustThrow([&] { fixture.load(); });
        };
        rejects([](Json& e) { e.erase("name"); });
        rejects([](Json& e) { e["description"] = ""; });
        rejects([](Json& e) { e["factionId"] = "missing"; });
        rejects([](Json& e) { e["cost"]["crystals"] = -1; });
        rejects([](Json& e) { e["cost"]["supply"] = 101; });
        rejects([](Json& e) { e["cost"]["crystals"] = 2.5; });
        rejects([](Json& e) { e["mobility"]["type"] = "teleport"; });
        rejects([](Json& e) { e["mobility"]["formationPriority"] = 0; });
        rejects([](Json& e) { e["attack"]["cooldownTicks"] = -1; });
        rejects([](Json& e) { e["stats"]["level"] = 0; });
        rejects([](Json& e) { e["unknown"] = 1; });
        auto fixture = original; fixture.catalog["entityFiles"].push_back("entities/humans.json");
        mustThrow([&] { fixture.load(); });
        fixture = original; fixture.entities["entities"][3]["production"]["trains"] = {"missing"};
        mustThrow([&] { fixture.load(); });
    });
    test("External progression and catalog paths reject corrupt data", [&] {
        CatalogFixture fixture(assets);
        for (const auto& thresholds : {Json::array({1, 100}), Json::array({0, 100, 100}), Json::array({0}), Json::array({0, -1}), Json::array({0, 1.5})}) {
            fixture.rules["hero"]["thresholds"] = thresholds; mustThrow([&] { fixture.load(); });
        }
        fixture.rules["hero"]["thresholds"] = {0, 100};
        fixture.rules["hero"]["experienceRadius"] = 0; mustThrow([&] { fixture.load(); });
        fixture.rules["hero"]["experienceRadius"] = 8;
        fixture.entities["entities"][2]["stats"]["level"] = 2; mustThrow([&] { fixture.load(); });
        fixture.entities["entities"][2]["stats"]["level"] = 1;
        fixture.catalog["entityFiles"] = {"../outside.json"}; mustThrow([&] { fixture.load(); });
    });
    test("Water records load independently of height and reject invalid placement", [&] {
        const auto file = rts::Paths::executable().parent_path() / L"test-water.rtsmap";
        auto j=mapJson(); j["terrain"]["surfaces"][3]="LLLSDL"; writeMap(file,j);
        const auto s = loadScenario(file);
        require(s.map.at({3, 3}).surface == rts::Surface::ShallowWater && s.map.at({4, 3}).surface == rts::Surface::DeepWater, "Water depth not loaded");
        j["terrain"]["surfaces"][0]="SLLLLL"; writeMap(file,j); mustThrow([&] { loadScenario(file); });
        j["terrain"]["surfaces"][0]="LLLLLL"; j["terrain"]["surfaces"][3]="LLLXLL"; writeMap(file,j); mustThrow([&] { loadScenario(file); });
        j["terrain"]["surfaces"][3]="LLLSDL"; j["resources"]={{{"cell",{3,3}},{"remaining",50}}}; writeMap(file,j); mustThrow([&] { loadScenario(file); });
    });
}
}
