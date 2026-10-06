#include "TestSupport.hpp"
#include "rts/TeamColor.hpp"
#include "rts/Library.hpp"

namespace rts::tests {
void dataTests(TestSuite& test, const TestContext& context) {
    test("Mana and untargeted abilities resolve from their data catalogs with strict validation", [&] {
        CatalogFixture fixture(context.assets);
        const auto definitions = fixture.load();
        require(definitions.entity("human.worker").maximumMana == 0, "Ordinary units gained a mana pool");
        const auto& hero = definitions.entity("human.hero");
        require(hero.maximumMana == 200 && hero.abilities.size() == 1 && hero.abilities.front().id == 1 &&
            definitions.ability(1).manaCost == 25 && definitions.ability(1).cooldownTicks == 150, "Hero placeholder ability data missing");
        auto& worker = fixture.entities["entities"][0];
        worker["stats"]["mana"] = 75; worker["abilities"] = Json::array({1});
        require(fixture.load().entity("human.worker").maximumMana == 75 &&
            fixture.load().entity("human.worker").abilities.front().id == 1, "Mana or ability is hardcoded to heroes");
        for (const auto& value : {Json(-1), Json(1000001), Json(1.5), Json("50"), Json(true), Json(nullptr)}) {
            worker["stats"]["mana"] = value; mustThrow([&] { fixture.load(); });
        }
        worker["stats"]["mana"] = 0;
        require(fixture.load().entity("human.worker").maximumMana == 0, "Zero mana was rejected");
        for (const auto& value : {Json::array({999}), Json::array({1, 1}), Json::array({0}), Json(nullptr)}) {
            worker["abilities"] = value; mustThrow([&] { fixture.load(); });
        }
        worker.erase("abilities");
        const auto original = fixture.abilities;
        fixture.abilities["abilities"].push_back(fixture.abilities["abilities"][0]); mustThrow([&] { fixture.load(); });
        for (const auto& value : {Json(-1), Json(1.5), Json("25")}) {
            fixture.abilities = original; fixture.abilities["abilities"][0]["manaCost"] = value;
            mustThrow([&] { fixture.load(); });
        }
        fixture.abilities = original; fixture.abilities["abilities"][0]["targeting"] = "point";
        mustThrow([&] { fixture.load(); });
        fixture.abilities = original; fixture.abilities["abilities"][0]["cooldownTicks"] = 0;
        mustThrow([&] { fixture.load(); });
    });
    test("Unit collision radius is catalog data with strict physical bounds", [&] {
        CatalogFixture fixture(context.assets);
        auto& motion = fixture.entities["entities"][0]["mobility"];
        motion["collisionRadius"] = .2f;
        require(std::abs(fixture.load().entity("human.worker").collisionRadius - .2f) < .00001f, "Radius not loaded");
        for (const auto& invalid : {Json(0), Json(-.1), Json(.501), Json("0.35"), Json(true), Json(nullptr)}) {
            motion["collisionRadius"] = invalid; mustThrow([&] { fixture.load(); });
        }
        motion.erase("collisionRadius"); mustThrow([&] { fixture.load(); });
    });
    test("Decomposition is a strict independent unit capability with a non-decomposing hero", [&] {
        const auto definitions = rts::Definitions::load(context.assets / "data/catalog.json");
        for (const auto& unit : definitions.entities()) if (unit.mobile)
            require(unit.canDecompose == !unit.hero.has_value(), "Prototype decomposition classification changed");
        CatalogFixture fixture(context.assets);
        auto& worker = fixture.entities["entities"][0];
        worker["canDecompose"] = false;
        require(!fixture.load().entity("human.worker").canDecompose, "Capability depends on hero or type ID");
        worker.erase("canDecompose"); require(fixture.load().entity("human.worker").canDecompose, "Missing capability must default to true");
        for (const auto& invalid : {Json(1), Json("true"), Json(nullptr)}) {
            worker["canDecompose"] = invalid; mustThrow([&] { fixture.load(); });
        }
    });
    test("Every command has a replaceable icon resolved from the asset catalog", [&] {
        const auto icons = rts::loadCommandIcons(context.worldPaths);
        for (const auto& command : rts::unitCommands) require(icons.contains(command.icon) && std::filesystem::is_regular_file(icons.at(command.icon).image), "Missing command image");
        require(icons.contains("idle-worker"), "Missing worker selector image");
    });
    const auto& assets=context.assets;
    test("Icon folders discover optional masks for every catalog entry and shared bundle", [&] {
        const auto root = rts::Paths::executable().parent_path() / "command-icon-test";
        std::filesystem::create_directories(root / "ui");
        const std::filesystem::path plain = L"icons/Без маски", painted = L"icons/Магия/Пламя";
        for (const auto& directory : {plain, painted}) {
            std::filesystem::create_directories(root / directory);
            std::filesystem::copy_file(assets / "ui/resources/crystal/icon.png", root / directory / "icon.png", std::filesystem::copy_options::overwrite_existing);
        }
        const auto mask = root / painted / "mask.png";
        std::filesystem::remove(mask);
        rts::Paths paths(root, root / "user");
        Json catalog;
        const auto utf8 = [](const std::filesystem::path& path) {
            const auto text = path.generic_u8string(); return std::string(text.begin(), text.end());
        };
        for (const auto& command : rts::unitCommands) catalog["icons"][command.icon] = utf8(plain);
        for (const auto id : {"idle-worker", "rally", "cancel", "attributes", "future.spell"}) catalog["icons"][id] = utf8(painted);
        const auto load = [&] { writeMap(root / "ui/commands.json", catalog); return rts::loadCommandIcons(paths); };
        for (const auto& [id, icon] : load())
            require(icon.mask.empty(), "A folder without mask.png acquired a mask");
        std::filesystem::copy_file(assets / "ui/resources/crystal/icon.png", mask, std::filesystem::copy_options::overwrite_existing);
        const auto icons = load();
        for (const auto id : {"idle-worker", "rally", "cancel", "attributes", "future.spell"})
            require(icons.at(id).image == paths.asset(painted / "icon.png") && icons.at(id).mask == paths.asset(painted / "mask.png"),
                "New, shared or auxiliary icon did not discover its mask");
        require(icons.at("move").mask.empty(), "Mask leaked into another folder");
        const auto direct = rts::loadIconAsset(paths, painted);
        require(direct.image == icons.at("future.spell").image && direct.mask == icons.at("future.spell").mask,
            "Generic icon loader depends on the command catalog");
        std::filesystem::remove(mask);
        require(load().at("future.spell").mask.empty(), "Reload retained a removed mask");
    });
    test("Icon folders require icon.png and reject invalid masks, paths and catalog entries", [&] {
        const auto root = rts::Paths::executable().parent_path() / "command-icon-invalid-test";
        std::filesystem::create_directories(root / "ui");
        std::filesystem::create_directories(root / "bundle");
        std::filesystem::copy_file(assets / "ui/resources/crystal/icon.png", root / "bundle/icon.png", std::filesystem::copy_options::overwrite_existing);
        rts::Paths paths(root, root / "user");
        mustThrow([&] { rts::loadIconAsset(paths, "missing"); });
        for (const auto directory : {"", "../outside", "C:/outside", "icons/file:stream"})
            mustThrow([&] { rts::loadIconAsset(paths, directory); });
        mustThrow([&] { paths.optionalAsset("../outside.png"); });
        std::filesystem::create_directory(root / "bundle/mask.png");
        mustThrow([&] { rts::loadIconAsset(paths, "bundle"); });
        std::filesystem::remove(root / "bundle/mask.png");
        Json catalog;
        for (const auto& command : rts::unitCommands) catalog["icons"][command.icon] = "bundle";
        for (const auto id : {"idle-worker", "rally", "cancel"}) catalog["icons"][id] = "bundle";
        const auto load = [&] { writeMap(root / "ui/commands.json", catalog); return rts::loadCommandIcons(paths); };
        auto& move = catalog["icons"]["move"];
        for (const auto& invalid : {Json(nullptr), Json(123), Json::object(), Json(""), Json("bundle/icon.png"), Json("../outside")}) {
            move = invalid; mustThrow([&] { load(); });
        }
        move = "bundle";
        catalog["icons"].erase("cancel"); mustThrow([&] { load(); });
    });
    test("Unit portrait and button artwork have independent optional paths with safe sprite fallbacks", [&] {
        CatalogFixture fixture(assets);
        const auto read = [&](const char* field) {
            const auto s = fixture.load().entity("human.worker").sprite;
            return std::string(field) == "portrait" ? s.portrait : s.icon;
        };
        auto& sprite = fixture.entities["entities"][0]["sprite"];
        for (const auto* field : {"portrait", "icon"}) {
            require(read(field).empty(), "Existing units lost the stand fallback");
            sprite[field] = "portraits/Рабочий.png";
            require(read(field) == "portraits/Рабочий.png", "Unit UI image path did not load");
            for (const auto path : {"../outside.png", "C:/outside.png", "portraits/file:stream"}) {
                sprite[field] = path; mustThrow([&] { fixture.load(); });
            }
            sprite[field] = nullptr;
            require(read(field).empty(), "Null UI artwork did not restore the stand fallback");
        }
        sprite["portrait"] = "portraits/Рабочий.png"; sprite["icon"] = "icons/Рабочий.png";
        require(read("portrait") == "portraits/Рабочий.png" && read("icon") == "icons/Рабочий.png",
            "Large preview and button artwork were mixed together");
    });
    test("Unit UI team masks require matching artwork and safe independent paths", [&] {
        CatalogFixture fixture(assets);
        auto& sprite = fixture.entities["entities"][0]["sprite"];
        for (const auto* field : {"portrait", "icon"}) {
            const auto mask = std::string(field) + "Mask";
            sprite[mask] = "portraits/Маска.png";
            mustThrow([&] { fixture.load(); });
            sprite[field] = "portraits/Рабочий.png";
            const auto s = fixture.load().entity("human.worker").sprite;
            require((mask == "portraitMask" ? s.portraitMask : s.iconMask) == "portraits/Маска.png", "UI mask path lost");
            for (const auto* path : {"../outside.png", "C:/outside.png", "portraits/file:stream"}) {
                sprite[mask] = path; mustThrow([&] { fixture.load(); });
            }
            sprite[mask] = nullptr;
            const auto unpainted = fixture.load().entity("human.worker").sprite;
            require((mask == "portraitMask" ? unpainted.portraitMask : unpainted.iconMask).empty(), "Null UI mask did not disable painting");
        }
    });
    const auto loadScenario = [&](const std::filesystem::path& file) { return rts::loadScenario(file, context.worldAssets, context.hallFootprint); };
    test("Library publishes selected catalog entries grouped by faction", [&] {
        CatalogFixture fixture(assets);
        auto defs = fixture.load();
        const auto published = rts::libraryEntries(defs, "humans");
        require(rts::libraryFactions(defs).size() == 1 && published.size() == 2 &&
            published[0]->id == "human.peacemaker" && published[1]->id == "human.hall", "Library lost its published entries or includes unfinished ones");
        fixture.catalog["factions"].push_back({{"id", "forest"}, {"name", "Лес"}, {"description", "Test faction"}});
        fixture.catalog["factions"].push_back({{"id", "empty"}, {"name", "Пусто"}, {"description", "Test faction"}});
        auto entity = fixture.entities["entities"][0];
        entity["id"] = "forest.worker"; entity["factionId"] = "forest"; entity["library"] = true;
        entity["name"] = "Хранитель"; entity["stats"]["health"] = 777;
        fixture.entities["entities"].push_back(entity);
        defs = fixture.load();
        const auto entries = rts::libraryEntries(defs, "forest");
        require(rts::libraryFactions(defs).size() == 2 && entries.size() == 1 && entries.front()->maximumHealth == 777 &&
            entries.front()->displayName == "Хранитель", "Library does not reflect live catalog metadata or leaks other races");
        for (auto& e : fixture.entities["entities"]) e["library"] = false;
        require(rts::libraryFactions(fixture.load()).empty(), "Empty factions remain visible in the library");
    });
    test("Vineyard separates cosmetic ground cover from solid props and leaves service lanes open", [&] {
        const auto& world = context.worldAssets;
        const auto scenario = rts::loadScenario(assets / "maps/demo.rtsmap", world, context.hallFootprint);
        auto bare = scenario;
        bare.landscape.paint.clear(); bare.landscape.decorations.clear();
        rts::rebuildScenario(bare);
        for (int y = 0; y < scenario.map.height(); ++y) for (int x = 0; x < scenario.map.width(); ++x) {
            const rts::Cell c{x, y};
            require(scenario.map.walkable(c) == bare.map.walkable(c) &&
                scenario.map.blocksVision(c) == bare.map.blocksVision(c), "Vineyard cosmetics changed navigation or sight");
        }
        for (const auto& d : world.objects()) if (d.id.starts_with("sunny_hills.vineyard.")) {
            if (!d.gameplay) {
                require(!d.blocksVision && std::none_of(d.collision.begin(), d.collision.end(), [](bool b) { return b; }),
                    "Grass or flowers became a blocker");
            } else {
                require(std::any_of(d.collision.begin(), d.collision.end(), [](bool b) { return b; }), "Solid prop has no collision");
            }
        }
        for (const auto& object : scenario.environment) if (object.definitionId.starts_with("sunny_hills.vineyard.")) {
            for (int y = 0; y < object.height; ++y) for (int x = 0; x < object.width; ++x) if (object.blocks(x, y)) {
                const auto c = object.origin + rts::Cell{x, y};
                require(!scenario.map.walkable(c) && scenario.map.walkable(c, rts::MovementType::Flying), "Solid vineyard prop lost ground-only collision");
            }
        }
        for (const auto goal : {rts::Cell{12, 8}, rts::Cell{15, 8}, rts::Cell{12, 11}, rts::Cell{15, 11},
                rts::Cell{12, 28}, rts::Cell{15, 29}, rts::Cell{19, 14}, rts::Cell{21, 16}, rts::Cell{10, 18}})
            require(rts::findPath(scenario.map, scenario.worker, goal).has_value(), "Vineyard blocked an aisle, arch, ramp or resource approach");
        auto restored = scenario;
        const auto vineAt = std::find_if(restored.environment.begin(), restored.environment.end(), [](const auto& o) { return o.definitionId == "sunny_hills.vineyard.vine_a"; });
        require(vineAt != restored.environment.end(), "Demo has no vineyard rows");
        auto& vine = *vineAt;
        const auto id = vine.id; const auto origin = vine.origin;
        vine.hitPoints = 0; rts::rebuildScenario(restored);
        require(restored.map.walkable(origin) && !restored.map.blocksVision(origin), "Destroyed vine retained collision or sight");
        vine.hitPoints = vine.maximumHitPoints; rts::rebuildScenario(restored);
        require(vine.id == id && !restored.map.walkable(origin) && restored.map.blocksVision(origin), "Vine restoration lost identity or blockers");
    });
    test("Sunny Hills showcases every asset and connects its asymmetric landscape through wide ramps", [&] {
        const auto& world=context.worldAssets;
        const auto s=rts::loadScenario(assets/"maps/sunny-hills.rtsmap",world,context.hallFootprint);
        require(s.map.width()==96&&s.map.height()==96,"Showcase must be 96 by 96");
        size_t objectCount=0,materialCount=0;
        for(const auto& d:world.objects()) if(d.id.starts_with("sunny_hills.")) {
            ++objectCount; int galleryCount=0;
            for(const auto& o:s.environment) if(o.definitionId==d.id&&o.origin.x>=69) ++galleryCount;
            for(const auto& o:s.landscape.decorations) if(o.definitionId==d.id&&o.position.x>=69) ++galleryCount;
            require(galleryCount==1,"Gallery must include exactly one of every object variant");
            if(!d.gameplay) require(!d.blocksVision&&std::none_of(d.collision.begin(),d.collision.end(),[](bool v){return v;}),"Cosmetic art affects traversal");
        }
        for(const auto& m:world.materials()) if(m.id.starts_with("sunny_hills.")) {
            ++materialCount;
            require(std::any_of(s.landscape.paint.begin(),s.landscape.paint.end(),[&](const auto& p){return p.material==m.id&&p.position.x>=69;}),"Material missing from gallery");
        }
        require(objectCount==185&&materialCount==6,"Imported variant count changed without updating the showcase");
        for(const rts::Cell goal: {rts::Cell{14,65},{24,24},{44,48},{34,10},{43,78},{67,60},{67,90}})
            require(rts::findPath(s.map,s.worker,goal).has_value(),"Showcase road, sanctuary, beach or gallery is unreachable");
        for(int y=0;y<96;++y) for(int x=0;x<96;++x) {
            const rts::Cell c{x,y};const auto& t=s.map.at(c);
            if(t.surface!=rts::Surface::Land) require(t.height==-1,"Sea must share one flat surface");
            if(t.ramp==rts::Cell{}) continue;
            const rts::Cell across{-t.ramp.y,t.ramp.x}; int width=1;
            const auto lane=[&](rts::Cell at){return s.map.contains(at)&&s.map.at(at).ramp==t.ramp&&s.map.at(at).height==t.height;};
            for(auto at=c+across;lane(at);at=at+across) ++width;
            for(auto at=c-across;lane(at);at=at-across) ++width;
            require(width==3&&s.map.canStep(c-t.ramp,c)&&s.map.canStep(c,c+t.ramp),"Showcase ramp narrowed or obstructed");
        }
        require(!s.map.canStep({40,53},{40,54})&&!s.map.walkable({26,43}),"Cliffs and dense groves no longer constrain movement");
    });
    test("Large demo: workers, friendly army, hostile camp and reachable plateaus", [&] {
        const auto definitions = rts::Definitions::load(assets / "data/catalog.json");
        rts::Simulation game(loadScenario(assets / "maps/demo.rtsmap"), {}, definitions.entity("human.worker"), definitions.entities());
        require(game.map().width() == 64 && game.map().height() == 64 && game.units().size() == 29, "Wrong demo dimensions or army");
        require(game.storedCrystals() == 300 && game.armySupply().used() == 44, "Enemy army consumed player supply");
        int friendly = 0, hostile = 0;
        for (const auto& u : game.units()) if (u.definition.attackDamage > 0) {
            if (u.owner == game.player().id) ++friendly;
            else {
                ++hostile;
                require(u.cell.x >= 50 && u.cell.y >= 50 && !game.fog().visible(u.cell), "Enemy camp not in unexplored southeast");
                require(rts::findPath(game.map(), game.worker().cell, u.cell).has_value(), "Enemy camp unreachable");
            }
        }
        require(friendly == 17 && hostile == 6, "Missing starting army");
        require(std::count_if(game.units().begin(), game.units().end(), [](const auto& u) {
            return u.definition.id == "human.peacemaker";
        }) == 13, "Demo ground melee troops were not all replaced by Peacemakers");
        require(std::count_if(game.units().begin(), game.units().end(), [&](const auto& u) {
            return u.definition.id == "human.peacemaker" && u.owner == game.player().id && u.cell == rts::Cell{18, 16};
        }) == 1, "Demo lost the Peacemaker beside the starting workers");
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
        fixture.commanders["commanders"][1]["startingWorker"] = "human.peacemaker";
        mustThrow([&] { fixture.load(); });
    });
    test("Commander rally art is optional, validates frames and loops in authored order", [&] {
        CatalogFixture fixture(assets);
        auto second = fixture.commanders["commanders"][0];
        second["id"] = "same-race-other-commander";
        second.erase("rallySprite");
        fixture.commanders["commanders"].push_back(second);
        const auto defs = fixture.load();
        const auto& art = *defs.commander("human_commander").rallySprite;
        require(!defs.commander("same-race-other-commander").rallySprite, "Rally art leaked to another commander of the same race");
        require(art.frames.size() == 6 && !art.teamMask.empty(), "Valeri lost six-frame masked rally art");
        for (std::uint64_t tick = 0; tick < 96; ++tick)
            require(&art.frame(tick) == &art.frames[(tick / 8) % 6], "Rally skipped, reordered or failed to loop a frame");
        require(rts::libraryEntries(defs, "humans").size() == 2, "Rally marker became a library entry");
        const auto original = fixture.commanders["commanders"][0]["rallySprite"];
        const auto rejects = [&](const std::function<void(Json&)>& change) {
            auto invalid = original; change(invalid);
            fixture.commanders["commanders"][0]["rallySprite"] = invalid;
            mustThrow([&] { fixture.load(); });
        };
        rejects([](Json& a) { a["image"] = "../rally.png"; });
        rejects([](Json& a) { a["teamMask"] = "C:/rally.png"; });
        rejects([](Json& a) { a["frames"] = Json::array(); });
        rejects([](Json& a) { a["frames"][0]["source"][2] = 0; });
        rejects([](Json& a) { a["frames"][0]["anchor"] = {8192, 0}; });
        rejects([](Json& a) { a["frames"][0]["anchor"] = {0}; });
        rejects([](Json& a) { a["ticksPerFrame"] = 0; });
        rejects([](Json& a) { a["light"]["radius"] = 0; });
        rejects([](Json& a) { a["light"]["intensity"] = 1.1; });
        rejects([](Json& a) { a["light"]["color"] = {256, 0, 0}; });
        rejects([](Json& a) { a["light"]["offset"] = {0}; });
        rejects([](Json& a) { a["scale"] = -1; });
        rejects([](Json& a) { a["typo"] = true; });
        fixture.commanders["commanders"][0]["rallySprite"] = nullptr;
        require(!fixture.load().commander("human_commander").rallySprite, "Null rally sprite did not select fallback");
    });
    test("Crystal maximum is enforced in map files and matches", [&] {
        require(rts::Crystal{}.remaining == 1000, "Default deposit is not full");
        const auto file = rts::Paths::executable().parent_path() / L"test-crystal-reserve.rtsmap";
        for (int amount : {-1, 0, 1, 1000, 1001}) {
            auto j=mapJson(); j["resources"]={{{"asset","crystal.small"},{"cell",{3,3}},{"remaining",amount}}}; writeMap(file,j);
            if (amount > 0 && amount <= 1000)
                require(loadScenario(file).crystals[0].remaining == amount, "Valid resource reserve was not loaded");
            else mustThrow([&] { loadScenario(file); });
        }
        mustThrow([] { rts::Simulation game(flatScenario(1001)); });
        mustThrow([] { rts::Simulation game(flatScenario(-1)); });
        const auto demo = loadScenario(assets / "maps/demo.rtsmap");
        require(!demo.crystals.empty() && std::all_of(demo.crystals.begin(), demo.crystals.end(), [](const auto& c) { return c.remaining == c.capacity; }), "Demo deposits are not full");
    });
    test("Crystal kinds round trip with catalog capacities and full footprints", [&] {
        const auto& world = context.worldAssets;
        const auto file = Paths::executable().parent_path() / L"test-crystal-kinds.rtsmap";
        struct Kind { const char* id; int width, height, capacity; };
        for (const auto kind : {Kind{"crystal.small", 1, 1, 1000}, Kind{"crystal.medium", 2, 1, 5500}, Kind{"crystal.big", 3, 2, 15000}}) {
            auto j = mapJson(12, 12);
            j["resources"] = {{{"asset", kind.id}, {"cell", {4, 4}}, {"remaining", kind.capacity}}};
            writeMap(file, j);
            const auto loaded = rts::loadScenario(file, world, {3, 2});
            const auto& node = loaded.crystals.front();
            require(node.width == kind.width && node.height == kind.height && node.capacity == kind.capacity, "Crystal definition lost");
            for (int y = 0; y < kind.height; ++y) for (int x = 0; x < kind.width; ++x)
                require(!loaded.map.walkable({4 + x, 4 + y}), "Crystal footprint is partially walkable");
            require(loaded.map.walkable({4 + kind.width, 4}) && loaded.map.walkable({4, 4 + kind.height}),
                "Crystal blocks cells outside its rectangular footprint");
            saveScenario(loaded, file);
            require(rts::loadScenario(file, world, {3, 2}).crystals.front().definitionId == kind.id, "Saved crystal changed type");
            j["resources"][0]["remaining"] = kind.capacity + 1; writeMap(file, j);
            mustThrow([&] { rts::loadScenario(file, world, {3, 2}); });
            j["resources"][0]["remaining"] = kind.capacity;
            j["resources"].push_back({{"asset", "crystal.small"}, {"cell", {3 + kind.width, 3 + kind.height}}, {"remaining", 1000}});
            writeMap(file, j); mustThrow([&] { rts::loadScenario(file, world, {3, 2}); });
        }
    });
    test("One entity schema includes names, descriptions, factions, costs and external progression", [&] {
        CatalogFixture fixture(assets); const auto defs = fixture.load();
        require(defs.entities().size() == 9, "Unified catalog lost records");
        mustThrow([&] { defs.entity("human.soldier"); });
        require(defs.entity("human.barracks").trainableUnits == std::vector<std::string>{"human.peacemaker", "human.archer", "human.catapult"},
            "Barracks did not replace the warrior with the Peacemaker");
        const auto& flyer = defs.entity("human.flying_soldier");
        const auto& archer = defs.entity("human.archer");
        require(airborne(flyer.movement) && !flyer.projectile && flyer.sprite.image == archer.sprite.image &&
            flyer.sprite.rows == archer.sprite.rows && flyer.sprite.walk == archer.sprite.walk &&
            flyer.sprite.windup == archer.sprite.windup && flyer.sprite.recovery == archer.sprite.recovery &&
            flyer.sprite.teamMask == archer.sprite.teamMask, "Flyer lost its temporary archer art or changed its gameplay");
        for (const auto& entity : defs.entities()) {
            require(!entity.displayName.empty() && !entity.description.empty() && entity.factionId == "humans", "Missing common metadata");
        }
        require(defs.entity("human.hero").hero && defs.entity("human.hero").maximumHealth == 600 && defs.entity("human.hero").hero->healthPerLevel == 60 && defs.entity("human.hero").cost.supply == 5, "Hero common properties missing");
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
    test("Catalog depot footprints reserve every cell and can rebind loaded maps", [&] {
        CatalogFixture fixture(assets);
        auto s = flatScenario(); s.worker = {6, 4};
        rts::rebuildScenario(s, {3, 2});
        for (const auto footprint : {rts::Cell{3, 2}, {4, 3}, {2, 1}}) {
            fixture.entities["entities"][3]["construction"]["footprint"] = {footprint.x, footprint.y};
            const auto definitions = fixture.load();
            const auto& depot = definitions.startingDepot("humans");
            require(depot.width == footprint.x && depot.height == footprint.y, "Depot lost catalog dimensions");
            rts::Simulation game(s, {}, definitions.entity("human.worker"), definitions.entities());
            for (int y = 0; y < 4; ++y) for (int x = 0; x < 5; ++x) {
                const auto cell = s.hall + rts::Cell{x, y};
                const bool inside = x < footprint.x && y < footprint.y;
                require(game.map().occupancy(cell) == unsigned(inside), "Depot occupancy has gaps, stale cells or duplicate reservations");
                require((game.buildingAt(cell) != nullptr) == inside, "Building hit detection disagrees with footprint");
            }
            require(game.map().occupancy({7, 7}) == 1, "Resizing depot changed crystal occupancy");
            const rts::WorldView view{{123, 345}, .8f};
            const auto& building = game.buildings().front();
            const auto bounds = rts::buildingBounds(game, building, view);
            const auto* stage = depot.buildingSprite.stage(depot.constructionTicks, depot.constructionTicks);
            const auto ground = view.project({s.hall.x + footprint.x * .5f, s.hall.y + footprint.y * .5f}, 0);
            require(std::abs(bounds.x + bounds.width * stage->anchor.x - ground.x) < .001f &&
                std::abs(bounds.y + bounds.height * stage->anchor.y - ground.y) < .001f, "Sprite is not anchored to the footprint center");
        }
        fixture.entities["entities"][3]["construction"]["footprint"] = {4, 3};
        const auto definitions = fixture.load();
        s.map.occupy({4, 3});
        mustThrow([&] { rts::Simulation game(s, {}, definitions.entity("human.worker"), definitions.entities()); });
        s.map.release({4, 3}); s.worker = {4, 3};
        mustThrow([&] { rts::Simulation game(s, {}, definitions.entity("human.worker"), definitions.entities()); });
    });
    test("Building art switches at exact percentages and validates authored stages", [&] {
        CatalogFixture fixture(assets);
        const auto defs = fixture.load();
        const auto& hall = defs.entity("human.hall");
        const auto& art = hall.buildingSprite;
        require(art.stages.size() == 4 && hall.width == 3 && hall.height == 2, "Hall must have four art stages and a 3x2 footprint");
        for (const auto [ticks, from] : {std::pair{0, 0}, {98, 0}, {99, 33}, {197, 33}, {198, 66}, {299, 66}, {300, 100}, {301, 100}})
            require(art.stage(ticks, 300) && art.stage(ticks, 300)->from == from, "Construction art switched at the wrong tick");
        require(art.stage(99, 301)->from == 0 && art.stage(100, 301)->from == 33, "Fractional threshold was rounded early");
        require(art.stage(198, 301)->from == 33 && art.stage(199, 301)->from == 66, "66 percent rounded early");
        require(!defs.entity("human.barracks").buildingSprite.stage(180, 180), "Unconfigured art lost fallback");
        const auto original = fixture.entities["entities"][3]["buildingSprite"];
        const auto rejects = [&](const std::function<void(Json&)>& change) {
            auto invalid = original; change(invalid);
            fixture.entities["entities"][3]["buildingSprite"] = invalid;
            mustThrow([&] { fixture.load(); });
        };
        rejects([](Json& a) { a["stages"][0]["from"] = 1; });
        rejects([](Json& a) { a["stages"][1]["from"] = 66; });
        rejects([](Json& a) { a["stages"][3]["from"] = 99; });
        rejects([](Json& a) { a["stages"][0]["image"] = "../outside.png"; });
        rejects([](Json& a) { a["stages"][0]["teamMask"] = "C:/mask.png"; });
        rejects([](Json& a) { a["stages"][0]["teamMask"] = "../mask.png"; });
        rejects([](Json& a) { a["stages"][0]["emissionMask"] = "../mask.png"; });
        rejects([](Json& a) { a["stages"][0]["emissive"] = "true"; });
        rejects([](Json& a) { a["stages"][0]["emissive"] = true; a["stages"][0]["emissionMask"] = "mask.png"; });
        rejects([](Json& a) { a["stages"][3]["layers"][0]["emissionMask"] = "C:/mask.png"; });
        rejects([](Json& a) { a["stages"][3]["lights"][1]["animationLayer"] = 30; });
        rejects([](Json& a) { a["stages"][3]["lights"][1]["animationLayer"] = 1; });
        rejects([](Json& a) { a["stages"][3]["lights"][1]["animationLayer"] = 2; });
        rejects([](Json& a) { a["stages"][0]["source"][2] = 0; });
        rejects([](Json& a) { a["stages"][0]["anchor"][0] = 2; });
        rejects([](Json& a) { a["scale"] = 0; });
        rejects([](Json& a) { a["stages"][0]["typo"] = true; });
        rejects([](Json& a) { a["stages"][3]["lights"][0]["radius"] = 0; });
        rejects([](Json& a) { a["stages"][3]["lights"][0]["intensity"] = 1.1; });
        rejects([](Json& a) { a["stages"][3]["lights"][0]["flicker"] = -1; });
        rejects([](Json& a) { a["stages"][3]["lights"][0]["color"] = {256, 0, 0}; });
        rejects([](Json& a) { a["stages"][3]["lights"][0]["position"] = {0}; });
        rejects([](Json& a) { a["stages"][3]["lights"][0]["when"] = "typo"; });
        rejects([](Json& a) { a["stages"][3]["layers"][0]["image"] = "../fire.png"; });
        rejects([](Json& a) { a["stages"][3]["layers"][0]["teamMask"] = "C:/mask.png"; });
        rejects([](Json& a) { a["stages"][3]["layers"][0]["frames"] = Json::array(); });
        rejects([](Json& a) { a["stages"][3]["layers"][0]["frames"][0][2] = 0; });
        rejects([](Json& a) { a["stages"][3]["layers"][0]["ticksPerFrame"] = 0; });
        rejects([](Json& a) { a["stages"][3]["layers"][0]["phase"] = 8; });
        rejects([](Json& a) { a["stages"][3]["layers"][0]["when"] = "typo"; });
        rejects([](Json& a) { a["stages"][3]["layers"][0]["destination"][2] = 0; });
        rejects([](Json& a) { a["stages"][3]["layers"][0]["destination"] = {1, 2}; });
        fixture.entities["entities"][3]["buildingSprite"] = original;
        fixture.entities["entities"][3]["buildingSprite"]["stages"][3]["teamMask"] = "sprites/buildings/valeri/ratusha/ratusha-team.png";
        require(!fixture.load().entity("human.hall").buildingSprite.stages.back().teamMask.empty(), "Explicit mask path lost");
        fixture.entities["entities"][3]["buildingSprite"]["stages"][3]["emissionMask"] = "sprites/window-emission.png";
        const auto masked = fixture.load().entity("human.hall").buildingSprite.stages.back();
        require(masked.emissionMask == "sprites/window-emission.png" && !masked.teamMask.empty(), "Emission mask replaced the team mask");
    });
    test("Building layers loop on simulation ticks and keep training effects separate", [&] {
        const auto definitions = rts::Definitions::load(assets / "data/catalog.json");
        const auto& art = definitions.entity("human.hall").buildingSprite;
        for (int progress : {0, 99, 198}) require(art.stage(progress, 300)->layers.empty(), "Finished effects appeared on construction");
        const auto& layers = art.stage(300, 300)->layers;
        require(layers.size() == 4, "Hall lost its fire, dome or braziers");
        require(layers[0].visible(false) && layers[1].visible(false), "Idle hall lost its crown");
        require(!layers[2].visible(false) && !layers[3].visible(false) && layers[2].visible(true) && layers[3].visible(true), "Training fire condition lost");
        require(layers[0].frame(0) == layers[0].frame(2) && layers[0].frame(3) != layers[0].frame(0), "Fire timing differs from 3 simulation ticks per frame");
        require(layers[0].frame(24) == layers[0].frame(0), "Fire does not loop");
        require(layers[1].frame(0) == layers[1].frame(999), "Dome animated");
        require(layers[2].frame(0) != layers[3].frame(0), "Braziers lost separate phases");
        const auto& stage = art.stages.back();
        const auto bounds = rts::buildingStageBounds(stage, art.scale, {400, 500}, 2);
        const auto overlay = rts::buildingLayerBounds(stage, layers[1], bounds);
        const auto& d = layers[1].destination;
        require(std::abs(overlay.x - bounds.x - d[0] * art.scale * 2) < .001f &&
            std::abs(overlay.y - bounds.y - d[1] * art.scale * 2) < .001f &&
            std::abs(overlay.width - d[2] * art.scale * 2) < .001f &&
            std::abs(overlay.height - d[3] * art.scale * 2) < .001f, "Overlay detached from base on zoom");
    });
    test("Color team masks preserve unpainted art, luminosity and premultiplied alpha", [] {
        // Gold outside mask, shaded red cloth, translucent cloth, soft mask edge.
        std::array<std::uint8_t, 16> pixels{30, 160, 220, 255, 20, 40, 200, 255, 10, 20, 100, 128, 20, 40, 200, 255};
        const std::array<std::uint8_t, 16> mask{0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 128, 128, 255};
        rts::applyTeamColorMask(pixels, mask, 0x4080ff);
        require(pixels[0] == 30 && pixels[1] == 160 && pixels[2] == 220 && pixels[3] == 255, "Unmasked gold changed");
        // Lum(base) = 85.8, Lum(team) = 122.77; SetLum shifts RGB by -36.97.
        require(pixels[4] == 218 && pixels[5] == 91 && pixels[6] == 27 && pixels[7] == 255, "Color blend differs from the reference vector");
        require(pixels[8] == 109 && pixels[9] == 46 && pixels[10] == 13 && pixels[11] == 128, "Alpha edge became opaque or invalid");
        require(pixels[12] == 119 && pixels[13] == 66 && pixels[14] == 113 && pixels[15] == 255, "Soft mask was not blended");
        auto unchanged = pixels;
        const std::array<std::uint8_t, 16> transparent{};
        rts::applyTeamColorMask(pixels, transparent, 0xff0000);
        require(pixels == unchanged, "Transparent mask changed pixels");
        mustThrow([&] { rts::applyTeamColorMask(pixels, std::span(mask).first(4), 0xffffff); });
    });
    test("Color blending keeps black shadows and white highlights through gamut clipping", [] {
        // Independent reference colors exercise both branches of ClipColor.
        const std::array<std::uint8_t, 4> whiteMask{255, 255, 255, 255};
        std::array<std::uint8_t, 4> red{0, 0, 255, 255};
        rts::applyTeamColorMask(red, whiteMask, 0x0000ff);
        require(red == std::array<std::uint8_t, 4>{255, 54, 54, 255}, "Bright base clipped without preserving luminosity");
        std::array<std::uint8_t, 4> blue{255, 0, 0, 255};
        rts::applyTeamColorMask(blue, whiteMask, 0xff0000);
        require(blue[0] == 0 && blue[1] == 0 && std::abs(int(blue[2]) - 94) <= 1 && blue[3] == 255,
            "Dark base clipped without preserving luminosity");
        const std::array<unsigned, 12> colors{0, 0xffffff, 0x808080, 0xff0000, 0x00ff00, 0x0000ff, 0x4080ff, 0xffd34d, 0xa354cc,
            0xffff00, 0xffbf00, 0xff8000};
        for (const auto color : colors) for (const int alpha : {1, 32, 128, 255}) for (const int shade : {0, alpha}) {
            const std::array<std::uint8_t, 4> original{std::uint8_t(shade), std::uint8_t(shade), std::uint8_t(shade), std::uint8_t(alpha)};
            auto pixel = original;
            rts::applyTeamColorMask(pixel, whiteMask, color);
            require(pixel == original, "Pure black/white or its alpha changed under Color blending");
        }
    });
    test("Color blending preserves luminosity and alpha over colors and mask coverage", [] {
        const std::array<unsigned, 7> teams{0x808080, 0xff0000, 0x00ff00, 0x0000ff, 0x4080ff, 0x40ffd3, 0xa354cc};
        const auto luminosity = [](const auto& pixel) { return .11 * pixel[0] + .59 * pixel[1] + .30 * pixel[2]; };
        for (const auto team : teams) for (const int alpha : {0, 1, 32, 128, 255})
            for (const int b : {0, 51, 153, 255}) for (const int g : {0, 51, 153, 255}) for (const int r : {0, 51, 153, 255}) {
                const std::array<std::uint8_t, 4> original{std::uint8_t(b * alpha / 255), std::uint8_t(g * alpha / 255),
                    std::uint8_t(r * alpha / 255), std::uint8_t(alpha)};
                for (const int coverage : {0, 64, 128, 192, 255}) {
                    const auto c = std::uint8_t(coverage);
                    const std::array<std::uint8_t, 4> mask{c, c, c, 255};
                    auto pixel = original;
                    rts::applyTeamColorMask(pixel, mask, team);
                    require(pixel[3] == alpha && pixel[0] <= alpha && pixel[1] <= alpha && pixel[2] <= alpha,
                        "Color blend changed transparency or produced invalid premultiplied RGB");
                    require(std::abs(luminosity(pixel) - luminosity(original)) <= .501,
                        "Color blend altered base luminosity beyond byte rounding");
                    if (!coverage) require(pixel == original, "Black mask modified the original");
                    if (coverage == 128) {
                        auto translucent = original;
                        const std::array<std::uint8_t, 4> softWhite{128, 128, 128, 128};
                        rts::applyTeamColorMask(translucent, softWhite, team);
                        require(translucent == pixel, "Mask alpha was multiplied twice");
                    }
                }
            }
    });
    test("White and black team paints preserve texture, mask coverage and alpha", [] {
        const std::array<std::uint8_t, 16> original{30, 160, 220, 255, 20, 40, 200, 255, 10, 20, 100, 128, 20, 40, 200, 255};
        const std::array<std::uint8_t, 16> mask{
            0, 0, 0, 255,
            255, 255, 255, 255,
            255, 255, 255, 255,
            128, 128, 128, 128};
        auto white = original, black = original;
        rts::applyTeamColorMask(white, mask, 0xffffff);
        rts::applyTeamColorMask(black, mask, 0);
        // Original unpremultiplied luminosity is 85.8 / 255 (42.9 / 128 at the alpha edge).
        require(white == std::array<std::uint8_t, 16>{30, 160, 220, 255, 143, 143, 143, 255, 71, 71, 71, 128, 82, 92, 171, 255},
            "White paint flattened the texture or applied mask alpha twice");
        require(black == std::array<std::uint8_t, 16>{30, 160, 220, 255, 29, 29, 29, 255, 14, 14, 14, 128, 24, 34, 114, 255},
            "Black paint flattened the texture or damaged the alpha edge");
        const std::array<std::uint8_t, 4> fullMask{255, 255, 255, 255};
        for (const int alpha : {0, 1, 32, 128, 255}) {
            int previousWhite = -1, previousBlack = -1;
            for (int shade = 0; shade <= alpha; ++shade) {
                const auto s = static_cast<std::uint8_t>(shade);
                const auto a = static_cast<std::uint8_t>(alpha);
                const std::array<std::uint8_t, 4> base{s, s, s, a};
                auto light = base, dark = base;
                rts::applyTeamColorMask(light, fullMask, 0xffffff);
                rts::applyTeamColorMask(dark, fullMask, 0);
                require(light[3] == a && dark[3] == a && light[0] <= alpha && dark[0] <= alpha,
                    "Neutral paint changed alpha or exceeded premultiplied bounds");
                require(light[0] >= s && dark[0] <= s && light[0] >= previousWhite && dark[0] >= previousBlack,
                    "Neutral paint reversed shading or moved luminosity in the wrong direction");
                if (alpha == 255 && shade >= 32 && shade <= 223)
                    require(light[0] > s && dark[0] < s, "White and black paints still produce identical midtones");
                previousWhite = light[0]; previousBlack = dark[0];
                for (const std::array<std::uint8_t, 4> emptyMask : {std::array<std::uint8_t, 4>{0, 0, 0, 255}, std::array<std::uint8_t, 4>{0, 0, 0, 0}}) {
                    light = dark = base;
                    rts::applyTeamColorMask(light, emptyMask, 0xffffff);
                    rts::applyTeamColorMask(dark, emptyMask, 0);
                    require(light == base && dark == base, "Neutral paint escaped the mask");
                }
            }
        }
    });
    test("Warm team paints brighten textured midtones without flattening shading or mask edges", [] {
        const std::array<std::uint8_t, 4> fullMask{255, 255, 255, 255};
        const auto luminosity = [](const auto& p) { return .11 * p[0] + .59 * p[1] + .30 * p[2]; };
        // Independent reference: shaded red cloth has L=85.8/255. Lifted yellow
        // is RGB(160,160,0), versus RGB(96,96,0) with unmodified Color blending.
        const std::array<std::uint8_t, 16> original{30, 160, 220, 255, 20, 40, 200, 255, 10, 20, 100, 128, 20, 40, 200, 255};
        const std::array<std::uint8_t, 16> mask{0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 128, 128, 128};
        auto yellow = original;
        applyTeamColorMask(yellow, mask, 0xffff00);
        require(yellow == std::array<std::uint8_t, 16>{30, 160, 220, 255, 0, 160, 160, 255, 0, 80, 80, 128, 10, 100, 180, 255},
            "Yellow lost its hue, did not brighten, or applied mask alpha twice");
        for (unsigned team : {0xffff00, 0xffbf00, 0xff8000}) for (int alpha : {0, 1, 32, 128, 255}) {
            double previous = -1;
            for (int shade = 0; shade <= alpha; ++shade) {
                const auto s = std::uint8_t(shade), a = std::uint8_t(alpha);
                const std::array<std::uint8_t, 4> base{s, s, s, a};
                auto painted = base, white = base;
                applyTeamColorMask(painted, fullMask, team);
                applyTeamColorMask(white, fullMask, 0xffffff);
                require(painted[3] == a && painted[0] <= a && painted[1] <= a && painted[2] <= a,
                    "Warm paint damaged premultiplied transparency");
                require(luminosity(painted) >= previous && std::abs(luminosity(painted) - luminosity(white)) <= 1,
                    "Warm paint flattened/reversed shading or failed to inherit white's brightness");
                previous = luminosity(painted);
                if (alpha == 255 && shade >= 32 && shade <= 223)
                    require(luminosity(painted) > shade + 15, "Warm midtones are still dull");
                for (int coverage : {0, 64, 128, 192, 255}) {
                    const auto c = std::uint8_t(coverage);
                    const std::array<std::uint8_t, 4> soft{c, c, c, c};
                    auto partial = base;
                    applyTeamColorMask(partial, soft, team);
                    for (int channel = 0; channel < 3; ++channel)
                        require(std::abs(partial[channel] - (base[channel] * (255 - coverage) + painted[channel] * coverage) / 255.0) <= 1,
                            "Warm correction escaped or doubled the soft mask");
                    require(partial[3] == a, "Soft warm mask changed sprite alpha");
                }
            }
        }
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
        rejects([](Json& e) { e["library"] = "yes"; });
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
        j["terrain"]["surfaces"][3]="LLLSDL"; j["resources"]={{{"asset","crystal.small"},{"cell",{3,3}},{"remaining",50}}}; writeMap(file,j); mustThrow([&] { loadScenario(file); });
    });
}
}
