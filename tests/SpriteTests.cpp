#include "TestSupport.hpp"
#include "rts/DirectionalSprite.hpp"
#include "rts/Renderer.hpp"
#include "render/DirectionalSpriteAssets.hpp"
#include "platform/WindowsSupport.hpp"

namespace rts::tests {
void spriteTests(TestSuite& test, const TestContext& context) {
    test("Identity and horizontal mirror preserve every source pixel including alpha", [&] {
        SpritePixels input{7, 13, std::vector<std::uint8_t>(7 * 13 * 4)};
        for (size_t i = 0; i < input.bgra.size(); ++i) input.bgra[i] = std::uint8_t(i % 251);
        require(warpSprite(input, {}, 3.5f).bgra == input.bgra, "Original facing was resampled or recoloured");
        const auto mirror = warpSprite(input, {}, 3.5f, true);
        for (int y = 0; y < 13; ++y) for (int x = 0; x < 7; ++x) for (int c = 0; c < 4; ++c)
            require(mirror.bgra[(y * 7 + x) * 4 + c] == input.bgra[(y * 7 + 6 - x) * 4 + c], "Mirror flipped vertical direction or channels");
        require(warpSprite(mirror, {}, 3.5f, true).bgra == input.bgra, "Double mirror changed source art");
    });
    test("Warp pins feet and preserves premultiplied soft outlines without holes", [&] {
        SpritePixels input{32, 30, std::vector<std::uint8_t>(32 * 30 * 4)};
        for (int y = 0; y < 30; ++y) for (int x = 5; x < 27; ++x) {
            auto* p = input.bgra.data() + (y * 32 + x) * 4;
            p[3] = x == 5 || x == 26 ? 80 : 255; p[0] = p[3]; p[1] = p[3] / 2; p[2] = 0;
        }
        SpriteWarp warp; warp.width = {.85f, .9f, 1.08f, 1.04f, 1, 1}; warp.shift = {.03f, -.02f, .01f, 0, 0, 0};
        const auto result = warpSprite(input, warp, 16);
        require(result.bgra != input.bgra, "Cardinal warp did nothing");
        for (size_t i = 0; i < result.bgra.size(); i += 4) {
            require(result.bgra[i] == result.bgra[i + 3] && result.bgra[i + 1] <= result.bgra[i + 3] && result.bgra[i + 2] == 0,
                "Filtering produced dark fringes, invalid premultiplication or new colours");
        }
        for (int y = 24; y < 30; ++y) for (int x = 0; x < 32 * 4; ++x)
            require(result.bgra[y * 32 * 4 + x] == input.bgra[y * 32 * 4 + x], "Warp moved the feet");
        for (int y = 0; y < 30; ++y) for (int x = 12; x < 20; ++x)
            require(result.bgra[(y * 32 + x) * 4 + 3] == 255, "Warp opened holes in an opaque body");
        warp.width[0] = 0; mustThrow([&] { warpSprite(input, warp, 16); });
        warp = {}; warp.shift.back() = .01f; mustThrow([&] { warpSprite(input, warp, 16); });
        input.bgra.pop_back(); mustThrow([&] { warpSprite(input, {}, 16); });
    });
    test("Eight rows retain two originals and the correct mirrored facings for every animation", [&] {
        DirectionalSpriteRecipe recipe; recipe.width = 8; recipe.height = 10; recipe.pivot = {4, 9};
        recipe.east.width = {.8f, .8f, .8f, .9f, 1, 1};
        std::array<std::array<SpritePixels, 9>, 2> frames;
        for (size_t direction = 0; direction < 2; ++direction) for (size_t column = 0; column < 9; ++column) {
            auto& image = frames[direction][column]; image = {8, 10, std::vector<std::uint8_t>(8 * 10 * 4)};
            for (int y = 0; y < 10; ++y) for (int x = 0; x < 8; ++x) {
                auto* p = image.bgra.data() + (y * 8 + x) * 4;
                p[0] = std::uint8_t(20 + 100 * direction + column); p[1] = std::uint8_t(x * 10); p[2] = std::uint8_t(y * 10); p[3] = 255;
            }
        }
        const auto atlas = assembleDirectionalSprite(recipe, frames);
        for (int column = 0; column < 9; ++column) for (int y = 0; y < 10; ++y) for (int x = 0; x < 8; ++x) for (int c = 0; c < 4; ++c) {
            const auto at = [&](int row, int xx) { return atlas.bgra[((row * 10 + y) * atlas.width + column * 8 + xx) * 4 + c]; };
            require(at(1, x) == frames[0][column].bgra[(y * 8 + x) * 4 + c] &&
                at(5, x) == frames[1][column].bgra[(y * 8 + x) * 4 + c], "SE or NW changed");
            require(at(7, x) == at(1, 7 - x) && at(3, x) == at(5, 7 - x) && at(6, x) == at(2, 7 - x),
                "SW/NE/W is not the exact horizontal mirror of SE/NW/E");
        }
    });
    test("Peacemaker animation follows logical facing, movement, blocking and attack phases", [&] {
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        Unit unit; unit.definition = definitions.entity("human.peacemaker");
        constexpr std::array<Cell, 8> directions{{{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}}};
        for (size_t row = 0; row < 8; ++row) {
            unit.facing = directions[row];
            require(unitFrame(unit).row == int(row) && unitFrame(unit).column == 0, "Stand direction does not match logical movement");
        }
        unit.route = {{1, 1}};
        for (int frame = 0; frame < 4; ++frame) {
            unit.walkCycle = frame / 4.0f;
            require(unitFrame(unit).column == frame + 1, "Four-frame walk lost its order");
        }
        unit.blockedTicks = 1; require(unitFrame(unit).column == 0, "Blocked unit kept walking");
        unit.attackPhase = AttackPhase::Windup; unit.attackTicks = 10;
        require(unitFrame(unit).column == 5, "Attack does not start at preparation");
        unit.attackTicks = 5; require(unitFrame(unit).column == 6, "Raised weapon frame missing");
        unit.attackPhase = AttackPhase::Recovery; unit.attackTicks = 10;
        require(unitFrame(unit).column == 7, "Impact art is not aligned to damage release");
        unit.attackTicks = 5; require(unitFrame(unit).column == 8, "Follow-through frame missing");
    });
    test("Directional catalog rejects incompatible layouts and unsafe recipe paths", [&] {
        CatalogFixture fixture(context.assets);
        auto& entities = fixture.entities["entities"];
        auto& sprite = std::find_if(entities.begin(), entities.end(), [](const auto& e) {
            return e["id"] == "human.peacemaker";
        })->at("sprite");
        const auto original = sprite;
        for (auto [key, value] : {std::pair{"teamMask", Json("blue")}, {"idle", Json(2)},
            {"walk", Json::array({0, 1, 2, 3})}, {"directionRecipe", Json("../outside.json")}}) {
            sprite = original; sprite[key] = value; mustThrow([&] { fixture.load(); });
        }
    });
    test("Shared death animation validates paths, crops, pivots and timing", [&] {
        CatalogFixture fixture(context.assets);
        auto& entities = fixture.entities["entities"];
        auto& sprite = std::find_if(entities.begin(), entities.end(), [](const auto& e) {
            return e["id"] == "human.peacemaker";
        })->at("sprite");
        const auto original = sprite["death"];
        for (auto [key, value] : {std::pair{"image", Json("../outside.png")}, {"scale", Json(0)},
            {"ticksPerFrame", Json(0)}, {"frames", Json::array()}}) {
            sprite["death"] = original; sprite["death"][key] = value; mustThrow([&] { fixture.load(); });
        }
        sprite["death"] = original; sprite["death"]["frames"][0]["source"][2] = 0;
        mustThrow([&] { fixture.load(); });
        sprite["death"] = original; sprite["death"]["frames"][0]["anchor"] = {8192, 8192};
        mustThrow([&] { fixture.load(); });
        sprite["death"] = nullptr;
        Corpse fallback; fallback.sprite = fixture.load().entity("human.peacemaker").sprite;
        require(!corpseFrame(fallback), "Missing death art did not retain the generic corpse fallback");
    });
    test("Supplied animations render eight living directions and one death sequence that holds its final frame", [&] {
        platform::ComApartment apartment;
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        const auto& definition = definitions.entity("human.peacemaker");
        const auto recipe = render::directionalRecipe(context.worldPaths, definition.sprite);
        require(recipe.sources[0][0].image == recipe.sources[1][0].image, "Stand sheet lost one source view");
        const auto output = Paths::executable().parent_path();
        Renderer renderer(Paths(context.assets, output / "sprite-test-data"));
        renderer.validateCombatAssets(definitions);
        renderer.snapshotUnitDirections(definition.sprite, output / "peacemaker-directions.png");
        // Exercise the normal gameplay renderer, lighting, selection and HUD as well as the overview.
        Scenario site{Map(28, 28), {8, 11}, {11, 11}, {}};
        site.units = {{"human.peacemaker", 0, {13, 14}}};
        Simulation game(std::move(site), {}, definitions.entity("human.worker"), definitions.entities());
        GameplayUi ui; ui.selection.ids = {game.units().back().id};
        renderer.snapshot(game, output / "peacemaker-in-game.png", nullptr, false, &ui);
        ui.selection.ids.push_back(game.worker().id);
        require(ui.selection.activeUnit(game)->definition.id == "human.peacemaker", "Group preview did not keep the Peacemaker active");
        renderer.snapshot(game, output / "peacemaker-group.png", nullptr, false, &ui);
        auto types = definitions.entities();
        for (auto& type : types) if (type.id == "human.peacemaker") {
            type.maximumHealth = type.attackDamage = 1; type.attackWindupTicks = 1;
        }
        Scenario duel{Map(28, 28), {8, 11}, {11, 11}, {}};
        duel.units = {{"human.peacemaker", 0, {13, 14}}, {"human.peacemaker", 1, {14, 14}}};
        Simulation battle(std::move(duel), {}, definitions.entity("human.worker"), types);
        ticks(battle, 2);
        require(battle.corpses().size() == 2, "Death preview did not kill both differently-facing Peacemakers");
        const auto& death = *definition.sprite.death;
        require(death.frames.size() == 4, "Supplied death sheet lost a frame");
        for (size_t frame = 0; frame < death.frames.size(); ++frame) {
            for (const auto& corpse : battle.corpses())
                require(corpseFrame(corpse)->source == death.frames[frame].source, "Death skipped a frame or depends on facing");
            renderer.snapshot(battle, output / ("peacemaker-death-" + std::to_string(frame) + ".png"));
            ticks(battle, death.ticksPerFrame);
        }
        ticks(battle, 300);
        for (const auto& corpse : battle.corpses())
            require(corpseFrame(corpse)->source == death.frames.back().source, "Corpse restarted its death animation");
        renderer.snapshot(battle, output / "peacemaker-corpse.png");
        require(context.worldAssets.bonesSprite().variants.size() == 8, "Supplied bones sheet lost a variation");
        ticks(battle, battle.corpses().front().remainingTicks);
        ticks(battle, Corpse::fadeTicks / 2);
        renderer.snapshot(battle, output / "peacemaker-fading.png");
        ticks(battle, Corpse::fadeTicks - Corpse::fadeTicks / 2);
        require(battle.corpses().empty() && battle.bones().size() == 2, "Body did not become a separate bones object");
        ticks(battle, 12);
        renderer.snapshot(battle, output / "peacemaker-bones.png");
        ticks(battle, battle.bones().front().remainingTicks + Bones::fadeTicks / 2);
        renderer.snapshot(battle, output / "bones-fading.png");
        ticks(battle, Bones::fadeTicks - Bones::fadeTicks / 2);
        require(battle.bones().empty(), "Fading bones survived their final removal");
        // Catalog syntax can be valid while a rectangle lies outside the actual PNG.
        CatalogFixture oversized(context.assets);
        for (auto& type : oversized.entities["entities"]) if (type["id"] == definition.id)
            type["sprite"]["death"]["frames"][0]["source"] = {8190, 8190, 470, 555};
        mustThrow([&] { renderer.validateCombatAssets(oversized.load()); });
    });
}
}
