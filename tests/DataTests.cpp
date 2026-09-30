#include "TestSupport.hpp"
#include "rts/TeamColor.hpp"
#include "rts/Library.hpp"

namespace rts::tests {
void dataTests(TestSuite& test, const TestContext& context) {
    const auto& assets=context.assets;
    const auto loadScenario = [&](const std::filesystem::path& file) { return rts::loadScenario(file, context.worldAssets, context.hallFootprint); };
    test("Library publishes selected catalog entries grouped by faction", [&] {
        CatalogFixture fixture(assets);
        auto defs = fixture.load();
        require(rts::libraryFactions(defs).size() == 1 && rts::libraryEntries(defs, "humans").size() == 1 &&
            rts::libraryEntries(defs, "humans").front()->id == "human.hall", "Initial library includes unfinished entries");
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
        const std::array<unsigned, 9> colors{0, 0xffffff, 0x808080, 0xff0000, 0x00ff00, 0x0000ff, 0x4080ff, 0xffd34d, 0xa354cc};
        for (const auto color : colors) for (const int alpha : {1, 32, 128, 255}) for (const int shade : {0, alpha}) {
            const std::array<std::uint8_t, 4> original{std::uint8_t(shade), std::uint8_t(shade), std::uint8_t(shade), std::uint8_t(alpha)};
            auto pixel = original;
            rts::applyTeamColorMask(pixel, whiteMask, color);
            require(pixel == original, "Pure black/white or its alpha changed under Color blending");
        }
    });
    test("Color blending preserves luminosity and alpha over colors and mask coverage", [] {
        const std::array<unsigned, 7> teams{0x808080, 0xff0000, 0x00ff00, 0x0000ff, 0x4080ff, 0xffd34d, 0xa354cc};
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
        j["terrain"]["surfaces"][3]="LLLSDL"; j["resources"]={{{"cell",{3,3}},{"remaining",50}}}; writeMap(file,j); mustThrow([&] { loadScenario(file); });
    });
}
}
