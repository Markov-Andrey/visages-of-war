#include "TestSupport.hpp"
#include "rts/Library.hpp"
#include "rts/MatchAssets.hpp"

namespace rts::tests {
namespace {
std::string libraryValue(const std::vector<LibraryDetail>& details, const std::string& label) {
    for (const auto& row : details) if (row.kind == LibraryDetailKind::Stat && row.label == label) return row.value;
    return {};
}
bool libraryText(const std::vector<LibraryDetail>& details, const std::string& text) {
    return std::any_of(details.begin(), details.end(), [&](const auto& row) { return row.label == text || row.value == text; });
}
}
void dataTests(TestSuite& test, const TestContext& context) {
    test("Library details follow changed catalog values and hide inapplicable capabilities", [&] {
        CatalogFixture fixture(context.assets);
        for (auto& entity : fixture.entities["entities"]) {
            if (entity["id"] == "church.slinger") {
                entity["stats"]["health"] = 481;
                entity["attack"]["range"] = 7.25;
                entity["attack"]["windupTicks"] = 15;
                entity["attack"]["recoveryTicks"] = 15;
                entity["attack"]["cooldownTicks"] = 30;
                entity["attack"]["targets"] = "air";
                entity["mobility"]["type"] = "amphibious";
                entity["mobility"]["speed"] = 2.75;
                entity["production"]["trainingTicks"] = 225;
            }
            if (entity["id"] == "church.hero") {
                entity["stats"]["mana"] = 321;
                entity["hero"]["healthPerLevel"] = 73;
                entity["hero"]["damagePerLevel"] = 9;
            }
        }
        fixture.abilities["abilities"][0]["manaCost"] = 17;
        fixture.abilities["abilities"][0]["cooldownTicks"] = 75;
        const auto definitions = fixture.load();
        const auto ranged = libraryDetails(definitions, definitions.entity("church.slinger"));
        require(libraryValue(ranged, "Здоровье") == "481" && libraryValue(ranged, "Дальность атаки") == "7.25 клеток" &&
            libraryValue(ranged, "Темп атаки") == "0.5 атак/с" && libraryValue(ranged, "Полный цикл атаки") == "2 с" &&
            libraryValue(ranged, "Допустимые цели") == "Воздушные", "Library combat values diverged from definitions");
        require(libraryValue(ranged, "Тип движения") == "Амфибия" && libraryValue(ranged, "Скорость") == "2.75 клеток/с" &&
            libraryValue(ranged, "Время обучения") == "7.5 с", "Library movement/training values diverged from definitions");
        require(libraryValue(ranged, "Мана").empty() && libraryValue(ranged, "Вместимость груза").empty() &&
            libraryValue(ranged, "Размер").empty() && !libraryText(ranged, "Развитие героя"), "Fighter shows inapplicable fields");
        const auto hero = libraryDetails(definitions, definitions.entity("church.hero"));
        const auto& ability = definitions.ability(1);
        require(libraryValue(hero, "Мана") == "321" && libraryValue(hero, "Здоровье за уровень") == "+73" &&
            libraryValue(hero, "Урон за уровень") == "+9" && libraryValue(hero, "Расход маны") == "17" &&
            libraryValue(hero, "Перезарядка") == "2.5 с" && libraryText(hero, ability.displayName) && libraryText(hero, ability.description),
            "Hero growth or resolved ability missing from library");
        require(libraryValue(hero, "Время обучения").empty(), "Untrainable starting hero shows a training time");
        const auto building = libraryDetails(definitions, definitions.entity("church.barracks"));
        require(libraryValue(building, "Размер") == "3 × 3 клетки" && libraryValue(building, "Время строительства") == "6 с" &&
            libraryValue(building, definitions.entity("church.slinger").displayName).find("7.5 с") != std::string::npos,
            "Production does not resolve the trainee's current definition");
        require(libraryValue(building, "Тип движения").empty() && libraryValue(building, "Дальность атаки").empty() &&
            libraryValue(building, "Мана").empty() && libraryValue(building, "Время обучения").empty(), "Building shows unit-only fields");
    });
    test("Library distinguishes direct credit from cargo and supports new entity identities", [&] {
        CatalogFixture fixture(context.assets);
        auto definitions = fixture.load();
        auto worker = definitions.entity("church.worker");
        worker.id = "new.worker";
        worker.carryCapacity = 99; // Inapplicable even if nonzero for direct credit.
        auto details = libraryDetails(definitions, worker);
        require(libraryValue(details, "За цикл добычи") == "7" && libraryValue(details, "Интервал добычи") == "10 с" &&
            libraryValue(details, "Зачисление") == "Сразу в запас игрока" && libraryValue(details, "Вместимость груза").empty() &&
            libraryValue(details, "Дальность атаки").empty(), "Direct worker shows cargo or attack parameters");
        worker.gatherAbility = definitions.ability(3);
        details = libraryDetails(definitions, worker);
        require(libraryValue(details, "За цикл добычи") == "1" && libraryValue(details, "Интервал добычи") == "0.333 с" &&
            libraryValue(details, "Вместимость груза") == "99" && libraryValue(details, "Зачисление") == "После доставки на склад",
            "Cargo harvesting parameters missing");
        worker.gatherAbility.reset();
        details = libraryDetails(definitions, worker);
        require(libraryValue(details, "За цикл добычи").empty() && libraryValue(details, "Вместимость груза").empty(),
            "Capacity alone exposes harvesting");
        auto hybrid = definitions.entity("church.flying_soldier");
        hybrid.constructible = true; // Independent capabilities must not hide each other.
        details = libraryDetails(definitions, hybrid);
        require(libraryValue(details, "Тип движения") == "Полёт" && libraryValue(details, "Допустимые цели") == "Воздушные" &&
            !libraryValue(details, "Размер").empty(), "Hybrid entity lost a capability or same-layer targets were not resolved");
        const auto artillery = libraryDetails(definitions, definitions.entity("church.catapult"));
        require(libraryValue(artillery, "Радиус поражения") == "1.4 клеток" && libraryValue(artillery, "Допустимые цели") == "Наземные",
            "Artillery parameters are missing");
    });
    test("Match assets include used factions and future production but exclude unrelated catalog blocks", [&] {
        CatalogFixture fixture(context.assets);
        fixture.catalog["factions"].push_back({{"id","test"},{"name","Test"},{"description","Unused race"}});
        auto unit = fixture.entities["entities"][0];
        unit["id"] = "test.worker"; unit["factionId"] = "test";
        fixture.entities["entities"].push_back(unit);
        const auto definitions = fixture.load();
        auto scenario = flatScenario();
        scenario.landscape.paint.push_back({"road", {5,5}});
        scenario.landscape.paint.push_back({"road", {6,5}});
        scenario.landscape.decorations.push_back({100, "vineyard", {8,8}});
        const auto& worker = definitions.entity("church.worker");
        const auto make = [&](Scenario map) {
            return Simulation(std::move(map), {}, worker, definitions.entities(), definitions.startingDepot(worker.factionId).id);
        };
        auto game = make(scenario);
        const auto plan = collectMatchAssets(game, definitions);
        require(plan.factions.size() == 1 && plan.factions.contains("church") &&
            plan.factions.at("church").entities.contains("church.barracks") &&
            plan.factions.at("church").entities.contains("church.slinger"), "Race preload omitted future production or loaded an unused race");
        require(plan.materials == std::set<std::string>{"grass", "road"} && plan.objects == std::set<std::string>{"vineyard"} &&
            plan.crystals == std::set<std::string>{"crystal.small"}, "Map asset selection is incomplete or duplicated");
        scenario.units.push_back({"test.worker", 1, {8,4}});
        auto enemyGame = make(scenario);
        const auto enemies = collectMatchAssets(enemyGame, definitions);
        require(enemies.factions.size() == 2 && enemies.factions.at("test").colors.contains(teamRgb(TeamColor::Red)),
            "Enemy race/team color was not preloaded");
        const std::array<std::string,1> reinforcements{"test.worker"};
        require(collectMatchAssets(game, definitions, reinforcements).factions.contains("test"), "Declared mission reinforcements were omitted");
        const std::array<std::string,1> invalid{"missing.worker"};
        mustThrow([&] { collectMatchAssets(game, definitions, invalid); });
    });
    test("Library builds factions and three exclusive categories from the entity catalog", [&] {
        CatalogFixture fixture(context.assets);
        auto definitions = fixture.load();
        require(libraryFactions(definitions).size() == 1 && libraryFactions(definitions).front()->id == "church",
            "Library lost the catalog faction");
        size_t total = 0;
        for (const auto tab : libraryTabs) {
            const auto entries = libraryEntries(definitions, "church", tab);
            require(!entries.empty(), "Church category is unexpectedly empty");
            total += entries.size();
            for (const auto* entity : entries) {
                if (tab == LibraryTab::Heroes) require(entity->hero.has_value(), "Nonhero entered heroes");
                else if (tab == LibraryTab::Buildings) require(entity->constructible && !entity->hero, "Building classification failed");
                else require(!entity->constructible && !entity->hero, "Building or hero duplicated under units");
            }
        }
        const auto units = libraryEntries(definitions, "church", LibraryTab::Units);
        require(total == definitions.entities().size() && std::any_of(units.begin(), units.end(),
            [](const auto* entity) { return entity->id == "church.worker"; }), "Library hid catalog entities or the worker");
        fixture.catalog["factions"].push_back({{"id","test"},{"name","Test"},{"description","Test race"}});
        auto worker = fixture.entities["entities"][0]; worker["id"]="test.worker"; worker["factionId"]="test";
        fixture.entities["entities"].push_back(worker);
        definitions = fixture.load();
        require(libraryFactions(definitions).size() == 2 && libraryEntries(definitions, "test", LibraryTab::Units).size() == 1 &&
            libraryEntries(definitions, "test", LibraryTab::Buildings).empty() && libraryEntries(definitions, "test", LibraryTab::Heroes).empty(),
            "New catalog race needs manual registration or empty categories contain foreign entries");
    });
    test("Harvest abilities resolve from the catalog and validate mode timing and capacity", [&] {
        CatalogFixture fixture(context.assets);
        const auto definitions = fixture.load();
        const auto& novice = definitions.entity("church.worker");
        require(novice.gatherAbility && novice.gatherAbility->id == 2 && novice.canGather() &&
            novice.harvesting().credit == HarvestCredit::Direct && novice.harvesting().intervalTicks == 300 &&
            novice.harvesting().amount == 7 && novice.harvesting().effect && novice.sprite.workOrigins &&
            novice.carryCapacity == 0, "Direct harvest ability missing");
        auto& worker = fixture.entities["entities"][0]["worker"];
        worker["gatherAbility"] = 3; mustThrow([&] { fixture.load(); });
        worker["carryCapacity"] = 10;
        require(fixture.load().entity("church.worker").harvesting().credit == HarvestCredit::Cargo, "Cargo ability unavailable");
        for (const auto& id : {Json(1),Json(999),Json(0),Json(2.5),Json(nullptr)}) {
            worker["gatherAbility"] = id; mustThrow([&] { fixture.load(); });
        }
        worker.erase("gatherAbility");
        const auto withoutAbility = fixture.load().entity("church.worker");
        require(withoutAbility.carryCapacity == 10 && !withoutAbility.canGather(), "Cargo capacity granted an implicit ability");
        Simulation game(flatScenario(), {}, withoutAbility, definitions.entities(), definitions.startingDepot(withoutAbility.factionId).id);
        require(!commandCapable(game.worker(), UnitCommand::Gather) &&
            !game.order(std::array{game.worker().id}, OrderKind::Gather, {7,7}), "Unit without harvest ability could gather");
        ticks(game, 120);
        require(game.storedCrystals() == 0 && game.crystals().front().remaining == 23, "Missing ability still credited crystals");
        worker["gatherAbility"] = 2;
        const auto original = fixture.abilities;
        for (const auto* field : {"intervalTicks", "amount"}) for (const auto& value : {Json(0),Json(-1),Json(1000001),Json(1.5),Json("1"),Json(nullptr)}) {
            fixture.abilities = original; fixture.abilities["abilities"][1]["harvest"][field] = value;
            mustThrow([&] { fixture.load(); });
        }
        fixture.abilities = original; fixture.abilities["abilities"][1]["harvest"]["credit"] = "other";
        mustThrow([&] { fixture.load(); });
        fixture.abilities = original; fixture.abilities["abilities"][1]["targeting"] = "none";
        mustThrow([&] { fixture.load(); });
        fixture.abilities = original; fixture.abilities["abilities"][1]["manaCost"] = 1;
        mustThrow([&] { fixture.load(); });
        fixture.abilities = original; fixture.entities["entities"][0]["abilities"] = Json::array({2});
        mustThrow([&] { fixture.load(); });
    });
    test("Construction mode is catalog data with worker-driven default and strict validation", [&] {
        CatalogFixture fixture(context.assets);
        const auto definitions = fixture.load();
        for (const auto& type : definitions.entities()) if (type.constructible)
            require(type.constructionMode == ConstructionMode::Summoning, "Church building is not summoned");
        auto& construction = fixture.entities["entities"][3]["construction"];
        construction["mode"] = "worker";
        require(fixture.load().entity("church.hall").constructionMode == ConstructionMode::Worker, "Worker construction cannot be selected");
        construction.erase("mode");
        require(fixture.load().entity("church.hall").constructionMode == ConstructionMode::Worker, "Missing mode must preserve worker construction");
        for (const auto& value : {Json("automatic"), Json(1), Json(true), Json(nullptr)}) {
            construction["mode"] = value; mustThrow([&] { fixture.load(); });
        }
    });
    test("Mana and untargeted abilities resolve from their data catalogs with strict validation", [&] {
        CatalogFixture fixture(context.assets);
        const auto definitions = fixture.load();
        require(definitions.entity("church.worker").maximumMana == 0, "Ordinary units gained a mana pool");
        const auto& hero = definitions.entity("church.hero");
        require(hero.maximumMana == 200 && hero.abilities.size() == 1 && hero.abilities.front().id == 1 &&
            definitions.ability(1).manaCost == 25 && definitions.ability(1).cooldownTicks == 150, "Hero placeholder ability data missing");
        auto& worker = fixture.entities["entities"][0];
        worker["stats"]["mana"] = 75; worker["abilities"] = Json::array({1});
        require(fixture.load().entity("church.worker").maximumMana == 75 &&
            fixture.load().entity("church.worker").abilities.front().id == 1, "Mana or ability is hardcoded to heroes");
        for (const auto& value : {Json(-1), Json(1000001), Json(1.5), Json("50"), Json(true), Json(nullptr)}) {
            worker["stats"]["mana"] = value; mustThrow([&] { fixture.load(); });
        }
        worker["stats"]["mana"] = 0;
        require(fixture.load().entity("church.worker").maximumMana == 0, "Zero mana was rejected");
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
        require(std::abs(fixture.load().entity("church.worker").collisionRadius - .2f) < .00001f, "Radius not loaded");
        for (const auto& invalid : {Json(0), Json(-.1), Json(.501), Json("0.35"), Json(true), Json(nullptr)}) {
            motion["collisionRadius"] = invalid; mustThrow([&] { fixture.load(); });
        }
        motion.erase("collisionRadius"); mustThrow([&] { fixture.load(); });
    });
    const auto& assets=context.assets;
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
    const auto loadScenario = [&](const std::filesystem::path& file) { return rts::loadScenario(file, context.worldAssets, context.hallFootprint); };
    test("Sunny Hills connects its compact village to every bench category and both water depths", [&] {
        const auto& world=context.worldAssets;
        const auto s=rts::loadScenario(assets/"maps/sunny-hills.rtsmap",world,context.hallFootprint);
        require(s.map.layoutSize()==Cell{96,64},"Showcase with Ink bench must be 96 by 64");
        const auto authoredX=[](Vec2 p) { return p.x-p.y+48; };
        const auto objectX=[&](const auto& o) { return authoredX({o.origin.x+o.width-.5f,o.origin.y+o.height-.5f}); };
        const auto cell=[](Cell p) { return Cell{int(std::floor(p.x*.5f+p.y+.75f)),int(std::floor(p.y-p.x*.5f+48.25f))}; };
        size_t objectCount=0,materialCount=0;
        for(const auto& d:world.objects()) if(d.id.starts_with("sunny_hills.")) {
            ++objectCount; int galleryCount=0;
            for(const auto& o:s.environment) if(o.definitionId==d.id&&objectX(o)>=40) ++galleryCount;
            for(const auto& o:s.landscape.decorations) if(o.definitionId==d.id&&authoredX(o.position)>=40) ++galleryCount;
            require(galleryCount==1,"Gallery must include exactly one of every object variant");
            if(!d.gameplay) require(!d.blocksVision&&std::none_of(d.collision.begin(),d.collision.end(),[](bool v){return v;}),"Cosmetic art affects traversal");
        }
        for(const auto& m:world.materials()) if(m.id.starts_with("sunny_hills.")) {
            ++materialCount;
            require(std::any_of(s.landscape.paint.begin(),s.landscape.paint.end(),[&](const auto& p){return p.material==m.id&&authoredX(p.position)>=40;}),"Material missing from gallery");
        }
        require(objectCount==185&&materialCount==6,"Imported variant count changed without updating the showcase");
        size_t inkObjects=0, inkMaterials=0;
        for (const auto& d : world.objects()) if (d.id.starts_with("sunny_hills_ink.")) {
            ++inkObjects;
            require(std::count_if(s.environment.begin(), s.environment.end(), [&](const auto& o) {
                return o.definitionId == d.id && objectX(o) >= 80;
            }) == 1, "Ink bench must show each new object exactly once");
            require(d.blocksVision == (d.id != "sunny_hills_ink.winery"), "Ink object sight rules differ from their category");
        }
        for (const auto& m : world.materials()) if (m.id.starts_with("sunny_hills_ink.")) {
            ++inkMaterials;
            require(std::any_of(s.landscape.paint.begin(), s.landscape.paint.end(), [&](const auto& p) {
                return p.material == m.id && authoredX(p.position) >= 80;
            }), "Ink material is missing from the eastern bench");
        }
        require(inkObjects == 22 && inkMaterials == 5, "Ink import lost variants");
        require(s.landscape.baseMaterial == "sunny_hills_ink.meadow", "Village did not adopt the new ground");
        for (const auto& o : s.environment) if (objectX(o) < 40)
            require(!o.definitionId.starts_with("sunny_hills.tree.") && o.definitionId != "sunny_hills.vineyard.winery" &&
                o.definitionId != "sunny_hills.vineyard.cellar" && o.definitionId != "sunny_hills.vineyard.vine_a" &&
                o.definitionId != "sunny_hills.vineyard.vine_b", "Village retained an old counterpart of new Ink art");
        for(const rts::Cell authoredGoal: {rts::Cell{21,32},{18,8},{29,20},{33,43},{6,32},{5,32},
                                  {40,28},{40,60},{40,13},{63,7},{68,7},{80,12},{80,33},{80,60},{88,55}}) {
            const auto goal=cell(authoredGoal);
            require(rts::findPath(s.map,s.worker,goal).has_value(),"Village, vineyard, beach or bench is unreachable");
            require(rts::findUnitPathTo(s.map,rts::center(s.worker),rts::center(goal),{},.35f,rts::MovementType::Walking).has_value(),
                "Village-to-bench route is too narrow for a unit collision circle");
        }
        const Cell shallow{40,19}, deep=cell({72,7});
        require(s.map.at(shallow).surface==Surface::ShallowWater && s.map.at(deep).surface==Surface::DeepWater,
            "Bench water samples missing");
        int waders=0;
        for(const auto& u:s.units) if(s.map.at(u.cell).surface==Surface::ShallowWater) {
            ++waders;
            require(u.definitionId=="church.peacemaker" && u.owner==0,"Water demonstration lost its wading peacemaker");
            require(findUnitPathTo(s.map,center(s.worker),center(u.cell),{},.35f,MovementType::Walking).has_value(),
                "Wading example cannot leave the water");
        }
        require(waders==2,"Both water demonstrations require a unit");
        require(std::abs(s.map.bedHeight(shallow,center(shallow))-s.map.bedHeight(deep,center(deep))-1)<.0001f,
            "Water bench lost its one-level bed drop");
        require(!findPath(s.map,s.worker,deep).has_value() && findPath(s.map,s.worker,deep,MovementType::Flying).has_value(),
            "Water bench no longer distinguishes ground and flying movement");
        for(const auto& o:s.environment) if(objectX(o)<40)
            require(!o.definitionId.starts_with("sunny_hills.ruin.")&&o.definitionId!="sunny_hills.vineyard.ruin",
                "Monumental ruins leaked into the rural composition");
        for(int y=0;y<s.map.height();++y) for(int x=0;x<s.map.width();++x) {
            const rts::Cell c{x,y};const auto& t=s.map.at(c);
            if(t.surface!=rts::Surface::Land) require(t.height==-1,"Sea must share one flat surface");
            if(t.ramp==rts::Cell{}) continue;
            const rts::Cell across{-t.ramp.y,t.ramp.x}; int width=1;
            const auto lane=[&](rts::Cell at){return s.map.contains(at)&&s.map.at(at).ramp==t.ramp&&s.map.at(at).height==t.height;};
            for(auto at=c+across;lane(at);at=at+across) ++width;
            for(auto at=c-across;lane(at);at=at-across) ++width;
            require(width==3&&s.map.canStep(c-t.ramp,c)&&s.map.canStep(c,c+t.ramp),"Showcase ramp narrowed or obstructed");
        }
        require(!s.map.canStep(cell({64,4}),cell({65,4})),
            "Shore cliffs or vineyard collision no longer constrain movement");
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
        auto game = makeTestSimulation(flatScenario());
        require(game.armySupply().used() == 1 && game.storedCrystals() == 0, "Initial worker supply");
    });
    test("Commander catalog accepts multiple commanders and resolves shared definitions", [&] {
        CatalogFixture fixture(assets);
        auto second = fixture.commanders["commanders"][0];
        second["id"] = "two"; second["name"] = "Second";
        fixture.commanders["commanders"].push_back(second);
        const auto defs = fixture.load();
        require(defs.commanders().size() == 2 && defs.commander("two").factionId == "church", "Commanders hardcoded");
        const rts::PlayerSettings player{0, "two", rts::TeamColor::Purple};
        auto game = makeTestSimulation(flatScenario(), player, defs.entity(defs.commander("two").startingWorker));
        require(game.player().commanderId == "two" && game.worker().owner == 0, "Ownership lost");
        fixture.commanders["commanders"][1]["startingWorker"] = "missing";
        mustThrow([&] { fixture.load(); });
        fixture.commanders["commanders"][1]["startingWorker"] = "church.peacemaker";
        mustThrow([&] { fixture.load(); });
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
        mustThrow([] { auto game = makeTestSimulation(flatScenario(1001)); });
        mustThrow([] { auto game = makeTestSimulation(flatScenario(-1)); });
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
        require(defs.entities().size() == 8, "Unified catalog lost records");
        mustThrow([&] { defs.entity("church.soldier"); });
        require(defs.entity("church.barracks").trainableUnits == std::vector<std::string>{"church.peacemaker", "church.slinger", "church.catapult"},
            "Barracks did not replace the warrior with the Peacemaker");
        const auto& flyer = defs.entity("church.flying_soldier");
        const auto& slinger = defs.entity("church.slinger");
        require(airborne(flyer.movement) && !flyer.projectile && flyer.sprite.image.empty(),
            "Flyer lost its placeholder or changed its gameplay");
        mustThrow([&] { defs.entity("church.archer"); });
        require(slinger.projectile && slinger.sprite.projectileOrigins && slinger.projectile->launchAngle == 15 &&
            slinger.projectile->source == std::array<int, 4>{1024, 0, 512, 512} &&
            slinger.projectile->anchor == Vec2{.5f, .5f} && !slinger.projectile->teamMask.empty(),
            "Slinger lost its normal charge, ball pivot, mask or release sockets");
        for (const auto& entity : defs.entities()) {
            require(!entity.displayName.empty() && !entity.description.empty() && entity.factionId == "church", "Missing common metadata");
        }
        require(defs.entity("church.hero").hero && defs.entity("church.hero").maximumHealth == 1000 && defs.entity("church.hero").hero->healthPerLevel == 60 && defs.entity("church.hero").cost.supply == 5, "Hero common properties missing");
        require(defs.entity("church.hall").constructible && !defs.entity("church.hall").mobile && defs.entity("church.hall").cost.crystals == 200, "Building not in common catalog");
        require(defs.progression().thresholds.size() == 10, "External progression lost");
        fixture.rules["hero"]["thresholds"] = {0, 10, 30};
        fixture.rules["hero"]["experiencePerVictimLevel"] = 7;
        const auto changed = fixture.load();
        auto scenario = flatScenario(); scenario.heroSpawn = rts::Cell{5, 5};
        rts::Simulation game(std::move(scenario), {}, changed.entity("church.worker"), changed.entities(), changed.startingDepot(changed.entity("church.worker").factionId).id, "church.hero", changed.progression());
        game.grantExperience(game.hero()->id, 30);
        require(game.hero()->level() == 3 && game.hero()->atMaxLevel(), "Match ignored external experience rules");
    });
    test("Faction selects its authored starting building independently of catalog order", [&] {
        CatalogFixture fixture(assets);
        fixture.catalog["factions"][0]["startingBuilding"] = "church.barracks";
        for (auto& entity : fixture.entities["entities"]) if (entity["id"] == "church.barracks") {
            entity["depot"] = true;
            entity["stats"]["health"] = 1234;
            entity["construction"]["footprint"] = {2, 2};
        }
        const auto definitions = fixture.load();
        const auto& worker = definitions.entity("church.worker");
        const auto& depot = definitions.startingDepot(worker.factionId);
        require(depot.id == "church.barracks", "Catalog chose the first depot instead of startingBuilding");
        auto types = definitions.entities();
        for (int order = 0; order < 2; ++order) {
            std::reverse(types.begin(), types.end());
            auto site = flatScenario(); site.worker = {6,4};
            Simulation game(site, {}, worker, types, depot.id);
            const auto& building = game.buildings().front();
            require(building.definition.id == depot.id && building.origin == site.hall && building.complete() &&
                building.health == 1234 && building.definition.width == 2 && building.definition.height == 2,
                "Starting building ignored authored type, position, health or footprint");
            require(game.map().occupancy(site.hall + Cell{1,1}) == 1 && game.map().occupancy(site.hall + Cell{2,0}) == 0,
                "Starting building reserved a hardcoded footprint");
        }
    });
    test("Playable factions reject missing or invalid starting building references", [&] {
        const CatalogFixture original(assets);
        for (const Json value : {Json("missing"), Json("church.worker"), Json(""), Json(nullptr)}) {
            auto fixture = original; fixture.catalog["factions"][0]["startingBuilding"] = value;
            mustThrow([&] { fixture.load(); });
        }
        auto missing = original; missing.catalog["factions"][0].erase("startingBuilding");
        mustThrow([&] { missing.load(); });
        for (int invalid = 0; invalid < 4; ++invalid) {
            auto fixture = original;
            fixture.catalog["factions"].push_back({{"id", "foreign"}, {"name", "Foreign"}, {"description", "Test faction"}});
            auto depot = fixture.entities["entities"][3]; depot["id"] = "test.start";
            if (invalid == 0) depot["construction"]["enabled"] = false;
            if (invalid == 1) { depot["mobility"]["enabled"] = true; depot["mobility"]["speed"] = 2; }
            if (invalid == 2) depot["depot"] = false;
            if (invalid == 3) depot["factionId"] = "foreign";
            fixture.entities["entities"].push_back(depot);
            fixture.catalog["factions"][0]["startingBuilding"] = "test.start";
            mustThrow([&] { fixture.load(); });
        }
    });
    test("Simulation never invents a missing starting building", [] {
        const EntityDefinition worker;
        const auto depot = fixtureDepot(worker);
        for (const std::string id : {std::string{}, std::string("missing"), worker.id})
            mustThrow([&] { Simulation game(flatScenario(), {}, worker, {worker, depot}, id); });
        mustThrow([&] { Simulation game(flatScenario(), {}, worker, {worker}, depot.id); });
        for (int invalid = 0; invalid < 4; ++invalid) {
            auto type = depot;
            if (invalid == 0) type.constructible = false;
            if (invalid == 1) type.mobile = true;
            if (invalid == 2) type.acceptsCargo = false;
            if (invalid == 3) type.factionId = "foreign";
            mustThrow([&] { Simulation game(flatScenario(), {}, worker, {worker, type}, type.id); });
        }
    });
    test("Catalog depot footprints reserve every cell and can rebind loaded maps", [&] {
        CatalogFixture fixture(assets);
        auto s = flatScenario(); s.worker = {6, 4};
        rts::rebuildScenario(s, {3, 2});
        for (const auto footprint : {rts::Cell{3, 2}, {4, 3}, {2, 1}}) {
            fixture.entities["entities"][3]["construction"]["footprint"] = {footprint.x, footprint.y};
            const auto definitions = fixture.load();
            const auto& depot = definitions.startingDepot("church");
            require(depot.width == footprint.x && depot.height == footprint.y, "Depot lost catalog dimensions");
            rts::Simulation game(s, {}, definitions.entity("church.worker"), definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id);
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
        mustThrow([&] { rts::Simulation game(s, {}, definitions.entity("church.worker"), definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id); });
        s.map.release({4, 3}); s.worker = {4, 3};
        mustThrow([&] { rts::Simulation game(s, {}, definitions.entity("church.worker"), definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id); });
    });
    test("Projectile sockets and artwork reject invalid catalog values", [&] {
        const CatalogFixture original(assets);
        const auto rejects = [&](const std::function<void(Json&)>& change) {
            auto fixture = original;
            for (auto& entity : fixture.entities["entities"]) if (entity["id"] == "church.slinger") change(entity);
            mustThrow([&] { fixture.load(); });
        };
        rejects([](Json& e) { e["sprite"]["projectileOrigins"] = {{.5, .5}}; });
        rejects([](Json& e) { e["sprite"]["projectileOrigins"][0] = {1.1, .5}; });
        rejects([](Json& e) { e["sprite"]["rows"][0] = 8; });
        rejects([](Json& e) { e["attack"]["projectile"] = nullptr; });
        rejects([](Json& e) { e["attack"]["projectile"]["anchor"] = {-.1, .5}; });
        rejects([](Json& e) { e["attack"]["projectile"]["launchAngle"] = -1; });
        rejects([](Json& e) { e["attack"]["projectile"]["launchAngle"] = 90; });
        rejects([](Json& e) { e["attack"]["projectile"]["teamMask"] = "../mask.png"; });
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
        rts::Simulation game(flatScenario(), {}, defs.entity("church.worker"), defs.entities(), defs.startingDepot(defs.entity("church.worker").factionId).id);
        require(game.buildingTypes().size() == 2 && !game.canPlace("forest.rooted", {5, 5}), "Another faction's building exposed");
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
        auto fixture = original; fixture.catalog["entityFiles"].push_back("entities/church.json");
        mustThrow([&] { fixture.load(); });
        fixture = original; fixture.entities["entities"][3]["production"]["trains"] = {"missing"};
        mustThrow([&] { fixture.load(); });
        fixture = original; fixture.entities["entities"][3].erase("buildingSprite");
        mustThrow([&] { fixture.load(); });
        fixture = original; fixture.entities["entities"][3]["buildingSprite"]["stages"] = Json::array();
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
