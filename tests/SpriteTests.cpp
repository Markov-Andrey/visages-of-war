#include "TestSupport.hpp"
#include "rts/DirectionalSprite.hpp"
#include "rts/Renderer.hpp"
#include "render/DirectionalSpriteAssets.hpp"
#include "platform/WindowsSupport.hpp"

namespace rts::tests {
void spriteTests(TestSuite& test, const TestContext& context) {
    test("Novice uses a work pose without attack artwork and retains eight directions", [&] {
        platform::ComApartment apartment;
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        const auto& type = definitions.entity("church.worker");
        const auto& sprite = type.sprite;
        require(type.displayName == "Послушница" && type.attackDamage == 0 && sprite.work == 5 &&
            sprite.teamMask == SpriteTeamMask::None && !sprite.portraitMask.empty() && !sprite.iconMask.empty(),
            "Novice gained an attack, lost its UI masks or work pose");
        require(sprite.frameWidth == 512 && sprite.frameHeight == 512 && !sprite.portrait.empty() && !sprite.icon.empty() &&
            sprite.death && sprite.death->frames.size() == 4 && !sprite.death->teamMask.empty(), "Novice artwork is incomplete");
        const auto recipe = render::directionalRecipe(context.worldPaths, sprite);
        require(recipe.sources[0].size() == 6 && recipe.sources[1].size() == 6 &&
            recipe.sources[0][5].source == std::array<int,4>{1024,0,512,512} &&
            recipe.sources[1][5].source == std::array<int,4>{1536,0,512,512}, "Work views were replaced with attack frames");
        for (const auto& direction : recipe.sources) for (const auto& frame : direction)
            require(frame.teamMask == sprite.death->teamMask, "Novice animation lost its aligned team mask");
        Unit unit; unit.definition = type; unit.facing = {1,0};
        for (const auto state : {UnitState::Harvesting, UnitState::Building}) {
            unit.state = state; require(unitFrame(unit).column == 5 && unitFrame(unit).row == 1, "Work pose not selected");
        }
        for (const auto state : {UnitState::Idle, UnitState::ToCrystal, UnitState::ToBuild, UnitState::ToHall}) {
            unit.state = state; require(unitFrame(unit).column == 0, "Work pose leaked into travel or idle");
        }
        unit.state = UnitState::Moving; unit.route = {{1,0}}; unit.position = {.1f,0}; unit.walkCycle = .5f;
        require(unitFrame(unit).column == 3, "Novice lost its four-frame walk");
        unit.definition.sprite.work = -1; unit.route.clear(); unit.state = UnitState::Harvesting;
        require(unitFrame(unit).column == 0, "Existing units lost their idle fallback while working");
        Renderer renderer(context.worldPaths);
        renderer.snapshotUnitDirections(sprite, Paths::executable().parent_path() / "novice-directions.png");
        Corpse corpse; corpse.sprite = sprite;
        require(corpseFrame(corpse)->source == sprite.death->frames.front().source, "Novice death starts at the wrong frame");
        CatalogFixture fixture(context.assets); auto& art = fixture.entities["entities"][0]["sprite"];
        art.erase("work"); require(fixture.load().entity("church.worker").sprite.work == -1, "Optional work pose has no fallback");
        for (const auto& value : {Json(-1),Json(256),Json(1.5),Json("5"),Json(nullptr)}) {
            art["work"] = value; mustThrow([&] { fixture.load(); });
        }
        auto malformed = recipe;
        std::array<std::vector<SpritePixels>,2> frames;
        frames[0].resize(6); frames[1].resize(5);
        mustThrow([&] { assembleDirectionalSprite(malformed, frames); });
    });

    test("Eight rows retain two originals and the correct mirrored facings for every animation", [&] {
        DirectionalSpriteRecipe recipe; recipe.width = 8; recipe.height = 10; recipe.pivot = {4, 9};
        recipe.east.width = {.8f, .8f, .8f, .9f, 1, 1};
        std::array<std::vector<SpritePixels>, 2> frames;
        for (auto& direction : frames) direction.resize(9);
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
    test("Simulation applies stable motion facing without delaying reversal, stop or attack", [] {
        const Scenario site{Map(24, 18), {1, 1}, {6, 8}, {}};
        EntityDefinition fighter; fighter.attackDamage = 10; fighter.movementPerSecond = 3; fighter.attackRange = 2;
        auto game = makeTestSimulation(site, {}, fighter);
        require(game.command({18, 8}), "Initial move rejected");
        ticks(game, 3);
        require(game.worker().facing == Cell{1, 0}, "Simulation did not face its movement");
        require(game.command({4, 8}), "Reverse move rejected"); game.tick();
        require(game.worker().facing == Cell{-1, 0}, "Reverse order waited on old motion history");
        game.stop();
        const auto position = game.worker().position;
        ticks(game, 10);
        require(game.worker().position == position && game.worker().facing == Cell{-1, 0}, "Stop changed position or facing");

        auto combatSite = site;
        auto enemy = fighter; enemy.id = "facing.enemy"; enemy.attackDamage = 0;
        combatSite.units = {{enemy.id, 1, {6, 7}}};
        auto combat = makeTestSimulation(std::move(combatSite), {}, fighter, {fighter, enemy});
        const auto id = combat.worker().id, target = combat.units().back().id;
        require(combat.command({18, 8}), "Combat setup move rejected"); ticks(combat, 3);
        require(combat.worker().facing == Cell{1, 0}, "Combat setup did not face east");
        const auto attackPosition = combat.worker().position;
        require(combat.attack(std::array{id}, target), "Attack rejected"); combat.tick();
        require(combat.worker().state == UnitState::Attacking && combat.worker().facing == Cell{-1, -1},
            "Movement smoothing delayed facing the attack target");
        require(combat.worker().position == attackPosition, "Facing the attack target changed collision position");
    });
    test("Directional catalog rejects incompatible layouts and unsafe recipe paths", [&] {
        CatalogFixture fixture(context.assets);
        auto& entities = fixture.entities["entities"];
        auto& sprite = std::find_if(entities.begin(), entities.end(), [](const auto& e) {
            return e["id"] == "church.peacemaker";
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
            return e["id"] == "church.peacemaker";
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
        sprite["death"] = original; sprite["death"]["teamMask"] = "../outside.png";
        mustThrow([&] { fixture.load(); });
        sprite["death"] = nullptr;
        Corpse fallback; fallback.sprite = fixture.load().entity("church.peacemaker").sprite;
        require(!corpseFrame(fallback), "Missing death art did not retain the generic corpse fallback");
    });
}
}
