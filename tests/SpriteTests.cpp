#include "TestSupport.hpp"
#include "rts/DirectionalSprite.hpp"
#include "rts/Renderer.hpp"
#include "rts/TeamColor.hpp"
#include "rts/SpriteIsland.hpp"
#include "render/DirectionalSpriteAssets.hpp"
#include "platform/WindowsSupport.hpp"

namespace rts::tests {
void spriteTests(TestSuite& test, const TestContext& context) {
    test("Raw sheet island crops exclude interleaved neighboring sprites without changing source pixels", [] {
        SpritePixels sheet{8, 5, std::vector<std::uint8_t>(8 * 5 * 4)};
        const auto set=[&](int x,int y,std::uint8_t alpha) {
            auto* p=sheet.bgra.data()+(y*8+x)*4; p[0]=alpha; p[1]=alpha/2; p[3]=alpha;
        };
        for(int y=1;y<4;++y) set(2,y,255);
        set(3,1,120); set(4,1,255); // Selected crown overlaps the neighbor's bounding rectangle.
        set(4,3,255); // Neighbor does not connect through the transparent gap.
        set(5,3,255); set(5,2,255);
        set(1,2,5); // Original low-alpha edge is retained.
        const auto original=sheet.bgra;
        const auto island=extractSpriteIsland(sheet,{1,0,6,5},{2,1});
        require(island.width==6&&island.height==5&&sheet.bgra==original,"Island changed the source or crop dimensions");
        require(island.bgra[(2*6+0)*4+3]==5&&island.bgra[(1*6+2)*4+3]==120,"Island lost original edge alpha");
        require(island.bgra[(3*6+3)*4+3]==0,"Interleaved neighbor leaked into crop");
        // Use a separate disconnected component in the upper-right corner.
        set(6,0,255);
        const auto clean=extractSpriteIsland(sheet,{1,0,6,5},{2,1});
        require(clean.bgra[(0*6+5)*4+3]==0,"Neighbor leaked into selected sprite");
        mustThrow([&] { extractSpriteIsland(sheet,{1,0,6,5},{0,1}); });
        mustThrow([&] { extractSpriteIsland(sheet,{1,0,6,5},{1,0}); });
        mustThrow([&] { extractSpriteIsland(sheet,{7,0,6,5},{7,1}); });
    });

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
            require(unitFrame(unit).row == (int(row) + 7) % 8 && unitFrame(unit).column == 0, "Stand direction does not match logical movement");
        }
        unit.route = {{1, 1}}; unit.position = {.1f, 0};
        for (int frame = 0; frame < 4; ++frame) {
            unit.walkCycle = frame / 4.0f;
            require(unitFrame(unit).column == frame + 1, "Four-frame walk lost its order");
        }
        unit.blockedTicks = 20;
        require(unitFrame(unit).column == 4, "Circle avoidance slid with an idle sprite");
        unit.tickPosition = unit.position;
        require(unitFrame(unit).column == 0, "Blocked unit kept walking");
        unit.attackPhase = AttackPhase::Windup; unit.attackTicks = 10;
        require(unitFrame(unit).column == 5, "Attack does not start at preparation");
        unit.attackTicks = 5; require(unitFrame(unit).column == 6, "Raised weapon frame missing");
        unit.attackPhase = AttackPhase::Recovery; unit.attackTicks = 10;
        require(unitFrame(unit).column == 7, "Impact art is not aligned to damage release");
        unit.attackTicks = 5; require(unitFrame(unit).column == 8, "Follow-through frame missing");
    });
    test("Motion facing holds sprite sectors through boundary noise and collision microsteps", [] {
        constexpr std::array<Cell, 8> headings{{{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}}};
        for (size_t direction = 0; direction < headings.size(); ++direction) for (float step : {.05f, .1f, .3f}) {
            const float angle = float(direction) * .7853981634f;
            const auto motion = [&](float offset, float fraction = 1.0f) {
                return Vec2{std::cos(angle + offset), std::sin(angle + offset)} * (step * fraction);
            };
            MotionFacing filter;
            Cell facing = filter.update({0, 1}, motion(0), step);
            require(facing == headings[direction], "New movement did not select its first facing");
            for (int tick = 0; tick < 90; ++tick) {
                // 21 and 26 degrees straddle the old eight-direction rounding boundary.
                facing = filter.update(facing, motion(tick % 2 ? .36651914f : .45378560f), step);
                require(facing == headings[direction], "Sector boundary noise flickered the sprite");
            }
            filter.reset(); facing = filter.update(facing, motion(0), step);
            for (int tick = 0; tick < 90; ++tick) {
                const Vec2 displacement = tick % 3 == 0 ? motion(3.14159265f, .05f) :
                    tick % 3 == 1 ? Vec2{} : motion(0);
                facing = filter.update(facing, displacement, step);
                require(facing == headings[direction], "Blocked ticks or reverse microsteps flipped the sprite");
            }
            for (int tick = 0; tick < 90; ++tick) {
                facing = filter.update(facing, motion(tick % 2 ? 1.57079633f : -1.57079633f), step);
                require(facing == headings[direction], "Alternating avoidance directions flickered the sprite");
            }
        }
    });
    test("Motion facing follows sustained turns promptly and commands discard old steering history", [] {
        for (const auto turn : {Cell{1, 1}, Cell{0, 1}, Cell{-1, 0}}) {
            MotionFacing filter;
            Cell facing = filter.update({0, 1}, {.1f, 0}, .1f);
            const Vec2 displacement = Vec2{float(turn.x), float(turn.y)} * (.1f / std::hypot(float(turn.x), float(turn.y)));
            for (int tick = 0; tick < 6; ++tick) facing = filter.update(facing, displacement, .1f);
            require(facing == turn, "A genuine turn still showed the old facing after 200 ms");
            for (int tick = 0; tick < 60; ++tick)
                require(filter.update(facing, displacement, .1f) == turn, "Settled facing oscillated on a straight path");
            filter.reset();
            require(filter.update(facing, {-.1f, -.1f}, .1f) == Cell{-1, -1}, "New order retained stale facing history");
        }
    });
    test("Simulation applies stable motion facing without delaying reversal, stop or attack", [] {
        const Scenario site{Map(24, 18), {1, 1}, {6, 8}, {}};
        EntityDefinition fighter; fighter.attackDamage = 10; fighter.movementPerSecond = 3; fighter.attackRange = 2;
        Simulation game(site, {}, fighter);
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
        Simulation combat(std::move(combatSite), {}, fighter, {fighter, enemy});
        const auto id = combat.worker().id, target = combat.units().back().id;
        require(combat.command({18, 8}), "Combat setup move rejected"); ticks(combat, 3);
        require(combat.worker().facing == Cell{1, 0}, "Combat setup did not face east");
        const auto attackPosition = combat.worker().position;
        require(combat.attack(std::array{id}, target), "Attack rejected"); combat.tick();
        require(combat.worker().state == UnitState::Attacking && combat.worker().facing == Cell{-1, -1},
            "Movement smoothing delayed facing the attack target");
        require(combat.worker().position == attackPosition, "Facing the attack target changed collision position");
    });
    test("Slower walk animation follows travelled distance without slowing straight or diagonal movement", [] {
        for (const auto destination : {Cell{12, 4}, Cell{12, 12}}) {
            EntityDefinition normal; normal.movementPerSecond = 3.2f;
            auto slower = normal; slower.sprite.walkCycleDistance = 2;
            const Scenario site{Map(16, 16), {1, 1}, {4, 4}, {}};
            Simulation baseline(site, {}, normal), adjusted(site, {}, slower);
            baseline.command(destination); adjusted.command(destination);
            auto previous = adjusted.worker().position;
            float distance = 0;
            for (int tick = 0; tick < 180; ++tick) {
                baseline.tick(); adjusted.tick();
                const auto& a = baseline.worker(); const auto& b = adjusted.worker();
                require(a.cell == b.cell && a.position == b.position && a.state == b.state,
                    "Walk animation tuning changed travel speed or arrival");
                distance += groundLength(b.position - previous);
                previous = b.position;
                const float expected = std::fmod(distance / slower.sprite.walkCycleDistance, 1.0f);
                const float delta = std::abs(b.walkCycle - expected);
                require(std::min(delta, 1 - delta) < .0001f, "Walk cycle skipped or restarted at a cell boundary");
            }
            require(adjusted.worker().cell == destination && adjusted.worker().state == UnitState::Idle,
                "Slower animation did not finish its move");
            const float stopped = adjusted.worker().walkCycle;
            ticks(adjusted, 30);
            require(adjusted.worker().walkCycle == stopped && unitFrame(adjusted.worker()).column == slower.sprite.idle,
                "Stopped unit continued its walk animation");
        }
    });
    test("Walk cycle distance is optional, positive and configurable per sprite", [&] {
        CatalogFixture fixture(context.assets);
        auto& sprite = fixture.entities["entities"][0]["sprite"];
        require(fixture.load().entity("human.worker").sprite.walkCycleDistance == 1, "Default walk tempo changed");
        require(fixture.load().entity("human.peacemaker").sprite.walkCycleDistance == 2, "Peacemaker lost its calmer gait");
        sprite["walkCycleDistance"] = 2;
        require(fixture.load().entity("human.worker").sprite.walkCycleDistance == 2, "Configured walk distance ignored");
        for (const auto& value : {Json(0), Json(-1), Json(17), Json("slow"), Json(nullptr)}) {
            sprite["walkCycleDistance"] = value; mustThrow([&] { fixture.load(); });
        }
    });
    test("Aligned Peacemaker cells preserve authored pixels and alpha under independent team paints", [&] {
        platform::ComApartment apartment;
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(wic.GetAddressOf()))), "WIC factory failed");
        const auto read = [&](const std::string& name) {
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            const auto path = context.worldPaths.asset(std::filesystem::path(std::u8string(name.begin(), name.end())));
            require(SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())), "Sprite decode failed");
            require(SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf())), "Sprite frame missing");
            require(SUCCEEDED(wic->CreateFormatConverter(converter.GetAddressOf())), "Sprite converter failed");
            require(SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)), "Sprite conversion failed");
            UINT width{}, height{}; require(SUCCEEDED(converter->GetSize(&width, &height)), "Sprite size missing");
            SpritePixels image{int(width), int(height), std::vector<std::uint8_t>(size_t(width) * height * 4)};
            require(SUCCEEDED(converter->CopyPixels(nullptr, width * 4, UINT(image.bgra.size()), image.bgra.data())), "Sprite pixels missing");
            return image;
        };
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        const auto& sprite = definitions.entity("human.peacemaker").sprite;
        const auto recipe = render::directionalRecipe(context.worldPaths, sprite);
        require(sprite.frameWidth == 512 && sprite.frameHeight == 512 && recipe.pivot == Vec2{256, 448}, "Aligned source grid changed");
        const auto original = read(sprite.image), mask = read(recipe.sources[0][0].teamMask);
        require(original.width == 2048 && original.height == 3072 && mask.width == original.width && mask.height == original.height,
            "Supplied sheet or mask dimensions changed");
        for (const auto color : {teamRgb(TeamColor::Blue), teamRgb(TeamColor::Red), 0u}) {
            const auto atlas = render::loadDirectionalSprite(context.worldPaths, wic.Get(), sprite, color);
            auto painted = original.bgra;
            applyTeamColorMask(painted, mask.bgra, color);
            size_t changed = 0, unpainted = 0;
            for (size_t direction = 0; direction < 2; ++direction) for (size_t column = 0; column < 9; ++column) {
                const auto& source = recipe.sources[direction][column];
                require(source.scale == 1 && source.source[2] == 512 && source.source[3] == 512 &&
                    source.pivot == Vec2{float(source.source[0] + 256), float(source.source[1] + 448)}, "Prepared cell was recentered or rescaled");
                for (int y = 0; y < 512; ++y) for (int x = 0; x < 512; ++x) {
                    const auto from = (size_t(source.source[1] + y) * original.width + source.source[0] + x) * 4;
                    const auto to = ((size_t(direction == 0 ? 1 : 5) * 512 + y) * atlas.width + column * 512 + x) * 4;
                    require(std::equal(painted.begin() + from, painted.begin() + from + 4, atlas.bgra.begin() + to), "Authored facing pixels were shifted, filtered or painted incorrectly");
                    require(atlas.bgra[to + 3] == original.bgra[from + 3], "Team paint changed transparency");
                    changed += !std::equal(original.bgra.begin() + from, original.bgra.begin() + from + 3, atlas.bgra.begin() + to);
                    unpainted += original.bgra[from + 3] && !mask.bgra[from + 3];
                }
            }
            require(changed > 0 && unpainted > 0, "Team paint did not distinguish equipment from unmasked artwork");
        }
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
        sprite["death"] = original; sprite["death"]["teamMask"] = "../outside.png";
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
        for (const auto* mask : {"portraitMask", "iconMask", "teamMask"}) {
            CatalogFixture invalid(context.assets);
            for (auto& type : invalid.entities["entities"]) if (type["id"] == definition.id) {
                if (std::string_view(mask) == "teamMask") type["sprite"]["death"][mask] = "sprites/worker.png";
                else type["sprite"][mask] = "sprites/worker.png";
            }
            mustThrow([&] { renderer.validateCombatAssets(invalid.load()); });
        }
    });
}
}
