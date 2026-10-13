#include "TestSupport.hpp"
#include "rts/DirectionalSprite.hpp"
#include "rts/Renderer.hpp"
#include "rts/HarvestVisual.hpp"
#include "render/DirectionalSpriteAssets.hpp"
#include "platform/WindowsSupport.hpp"

namespace rts::tests {
void spriteTests(TestSuite& test, const TestContext& context) {
    test("All ability effects start the same animation and never produce a weapon hit", [] {
        for (int effect = 0; effect < 3; ++effect) {
            auto type = fixtureWorker(); type.attackDamage = 0; type.maximumMana = 50;
            type.sprite.idle = 2; type.sprite.windup = {4, 5}; type.sprite.recovery = {6, 7};
            type.attackWindupTicks = type.attackRecoveryTicks = 4;
            AbilityDefinition ability{71 + effect, "Action", "", 5, 20}; ability.animation = AbilityAnimation::Attack;
            if (effect == 1) { ability.targeting = AbilityTargeting::Building; ability.repair = RepairDefinition{75, 0}; }
            if (effect == 2) {
                ability.targeting = AbilityTargeting::Crystal; ability.manaCost = ability.cooldownTicks = 0;
                ability.harvest = HarvestDefinition{HarvestCredit::Direct, 1000, 1}; type.abilities = {ability}; type.abilities.front().targeting = AbilityTargeting::Crystal; type.abilities.front().targets.relations = {TargetRelation::Neutral};
            } else type.abilities = {ability};
            Scenario scene{Map(24, 24), {3, 3}, {3, 5}, {{{2, 5}, 1000}}}; scene.map.occupy({2, 5});
            auto game = makeTestSimulation(std::move(scene), {}, type);
            auto& building = const_cast<Building&>(game.buildings().front()); building.health = 500;
            const auto id = game.worker().id;
            require(effect == 2 ? game.command({2, 5}) : game.activateAbility(id, ability.id, effect == 1 ? building.id : 0),
                "Ability activation rejected");
            for (int tick = 0; tick < 8; ++tick) {
                require(unitFrame(game.worker()).column == 4 + tick / 2, "Ability effect changed animation startup or timing");
                require(game.worker().attackPhase == AttackPhase::Ready && game.projectiles().empty(), "Animation fired a weapon");
                game.tick();
            }
            require(game.worker().mana == 50 - ability.manaCost, "Animation or sustained work charged mana twice");
            if (effect == 0) require(!game.worker().abilityAnimation.current() && unitFrame(game.worker()).column == 2,
                "Instant ability did not return to stand after one cycle");
            else require(game.worker().abilityAnimation.current(), "Active channel lost its animation");
            if (effect == 2) {
                auto& caster = const_cast<Unit&>(game.worker());
                AbilityDefinition instant{190, "Instant over channel", "", 1, 60}; instant.animation = AbilityAnimation::Work;
                caster.definition.sprite.work = 9; caster.definition.abilities.push_back(instant);
                require(game.activateAbility(id, instant.id), "Instant cast during gathering rejected");
                ticks(game, 5);
                require(unitFrame(game.worker()).column == 9 && game.worker().currentOrder.kind == OrderKind::Gather,
                    "Sustained work overwrote a new cast or cast replaced gathering");
                ticks(game, Simulation::ticksPerSecond - 5);
                require(game.worker().abilityAnimation.current() && game.worker().abilityAnimation.current()->abilityId == ability.id,
                    "Sustained action did not resume after instant animation");
            }
            game.stop(); require(!game.worker().abilityAnimation.current() && unitFrame(game.worker()).column == 2,
                "Stop retained an ability animation");
        }
    });
    test("Animation-only casts preserve orders and failed casts cannot restart playback", [] {
        auto type = fixtureWorker(); type.attackDamage = 0; type.maximumMana = 10;
        AbilityDefinition ability{831, "Instant", "", 5, 60}; ability.animation = AbilityAnimation::Attack;
        type.abilities = {ability};
        auto game = makeTestSimulation(flatScenario(), {}, type); const auto id = game.worker().id;
        game.order(std::array{id}, OrderKind::Move, {5, 6});
        require(game.activateAbility(id, 831), "Instant cast rejected");
        const auto before = game.worker().position; game.tick();
        require(game.worker().currentOrder.kind == OrderKind::Move && game.worker().position != before,
            "Cosmetic animation replaced movement order or stopped movement");
        const int elapsed = game.worker().abilityAnimation.current()->elapsedTicks;
        require(!game.activateAbility(id, 831) && !game.activateAbilityFor(1, id, 831) &&
            game.worker().abilityAnimation.current()->elapsedTicks == elapsed && game.worker().mana == 5,
            "Rejected activation restarted animation or charged mana");
        game.stop(); ticks(game, 60);
        auto& caster = const_cast<Unit&>(game.worker()); caster.mana = 0;
        require(!game.activateAbility(id, 831) && !caster.abilityAnimation.current(), "Insufficient mana played animation");
        caster.mana = 10;
        require(!game.activateAbility(id, 831, 99999) && !caster.abilityAnimation.current(), "Invalid target played animation");
        caster.definition.abilities[0].animation.reset();
        require(game.activateAbility(id, 831) && !caster.abilityAnimation.current(), "Missing animation field started playback");
    });
    test("Ability animation tags select available actions and missing artwork falls back to stand", [] {
        for (auto action : {AbilityAnimation::Stand, AbilityAnimation::Work, AbilityAnimation::Attack}) {
            auto type = fixtureWorker(); type.attackDamage = 0; type.sprite.idle = 2; type.sprite.work = 9;
            type.sprite.windup = {6}; type.sprite.recovery = {7};
            AbilityDefinition ability{91, "Instant", "", 0, 1}; ability.animation = action; type.abilities = {ability};
            auto game = makeTestSimulation(flatScenario(), {}, type);
            require(game.activateAbility(game.worker().id, 91), "Cosmetic cast failed");
            require(unitFrame(game.worker()).column == (action == AbilityAnimation::Work ? 9 : action == AbilityAnimation::Attack ? 6 : 2),
                "Action tag ignored");
            auto& unit = const_cast<Unit&>(game.worker());
            unit.definition.sprite.work = -1; unit.definition.sprite.windup.clear();
            require(unitFrame(unit).column == 2, "Unavailable action did not fall back to stand");
        }
    });
    test("Harvest channel stops with orders, death and hidden workers or deposits", [&] {
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        Scenario scene{Map(32, 32), {1, 1}, {6, 7}, {{{7, 7}, 23}, {{30, 30}, 23}}};
        scene.map.occupy({7, 7}); scene.map.occupy({30, 30});
        auto game = makeTestSimulation(std::move(scene), {}, definitions.entity("church.worker"));
        require(game.command({7, 7}), "Harvest channel fixture rejected order");
        for (int i = 0; i < 100 && game.worker().state != UnitState::Harvesting; ++i) game.tick();
        require(harvestEffectTarget(game, game.worker()), "Active harvesting channel missing");
        auto unit = game.worker(); unit.health = 0;
        require(!harvestEffectTarget(game, unit), "Dead worker still channels");
        unit = game.worker(); unit.cell = {game.map().width() - 1, game.map().height() - 1};
        require(!harvestEffectTarget(game, unit), "Hidden worker reveals channel");
        unit = game.worker(); unit.targetCrystal = 1;
        require(!harvestEffectTarget(game, unit), "Hidden deposit reveals channel");
        unit = game.worker(); unit.targetCrystal = -1;
        require(!harvestEffectTarget(game, unit), "Invalid harvest target accepted");
        game.stop(); require(!harvestEffectTarget(game, game.worker()), "Stopped worker still channels");
    });
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
        Unit unit; unit.definition = type; unit.health = type.maximumHealth; unit.facing = {1,0};
        for (const auto state : {UnitState::Harvesting, UnitState::Building}) {
            unit.abilityAnimation.clear();
            if (state == UnitState::Harvesting) unit.abilityAnimation.sustain(*type.gatherAbility(), type);
            unit.state = state; require(unitFrame(unit).column == 5 && unitFrame(unit).row == 1, "Work pose not selected");
        }
        unit.abilityAnimation.clear();
        for (const auto state : {UnitState::Idle, UnitState::ToCrystal, UnitState::ToBuild, UnitState::ToHall}) {
            unit.state = state; require(unitFrame(unit).column == 0, "Work pose leaked into travel or idle");
        }
        unit.state = UnitState::Moving; unit.route = {{1,0}}; unit.position = {.1f,0}; unit.walkCycle = .5f;
        require(unitFrame(unit).column == 3, "Novice lost its four-frame walk");
        unit.definition.sprite.work = -1; unit.route.clear(); unit.state = UnitState::Harvesting;
        unit.abilityAnimation.sustain(*type.gatherAbility(), type);
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
