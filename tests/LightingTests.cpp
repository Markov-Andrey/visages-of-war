#include "TestSupport.hpp"
#include "render/RenderSupport.hpp"
#include "render/TerrainGrid.hpp"
#include "rts/UnitOcclusion.hpp"

namespace rts {
struct RendererLightingTest {
    static void gridContours(const tests::TestContext& context) {
        using render::check;
        using tests::require;
        platform::ComApartment apartment;
        Renderer renderer(Paths(context.assets, Paths::executable().parent_path() / "grid-test-data"));
        Renderer::ComPtr<IWICBitmap> output;
        constexpr UINT width = 256, height = 256;
        check(renderer.wic_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, output.GetAddressOf()));
        auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
        properties.dpiX = properties.dpiY = 96;
        check(renderer.factory_->CreateWicBitmapRenderTarget(output.Get(), properties, renderer.target_.GetAddressOf()));
        check(renderer.target_->CreateSolidColorBrush(D2D1::ColorF(0), renderer.brush_.GetAddressOf()));
        Map map(8, 8);
        const auto draw = [&](Cell c, const WorldView& view, bool cached) {
            renderer.target_->BeginDraw(); renderer.target_->Clear(D2D1::ColorF(0));
            if (cached) renderer.gridFootprint(map, c, view, 0xe16d65);
            else {
                const auto p = view.project(center(c), float(map.at(c).height));
                auto corners = map.surfaceCorners(c, view);
                for (auto& v : corners) v = p + (v - p) * .85f;
                renderer.polygon(corners, 0xe16d65, .7f, false);
            }
            check(renderer.target_->EndDraw());
            std::vector<BYTE> pixels(width * height * 4);
            check(output->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data()));
            return pixels;
        };
        for (Cell c : {Cell{1, 1}, Cell{5, 6}}) for (int elevation : {-1, 3})
            for (Cell ramp : {Cell{}, Cell{1, 0}, Cell{-1, 0}, Cell{0, 1}, Cell{0, -1}})
                for (float zoom : {.35f, .85f, 2.f}) {
                    map.at(c).height = elevation; map.at(c).ramp = ramp;
                    WorldView view{{}, zoom};
                    view.origin = Vec2{128.3f, 128.7f} - view.project(center(c), float(elevation));
                    const auto expected = draw(c, view, false), actual = draw(c, view, true);
                    int difference = 0;
                    for (size_t i = 0; i < actual.size(); ++i) difference += std::abs(int(actual[i]) - int(expected[i]));
                    require(difference < 1000, "Cached grid contour changed shape, position or stroke width after a terrain/camera edit");
                }
        require(renderer.gridFootprints_.size() == 5, "Contour cache grows with camera, elevation or cell position");
        renderer.discardTarget();
        require(renderer.gridFootprints_.empty(), "Discarded renderer retained grid resources");
    }
    static void water(const tests::TestContext& context) {
        using render::check;
        using tests::require;
        platform::ComApartment apartment;
        const auto root = Paths::executable().parent_path();
        Renderer renderer(Paths(context.assets, root / "water-test-data"));
        Renderer::ComPtr<IWICBitmap> output;
        constexpr UINT width = 640, height = 384;
        check(renderer.wic_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, output.GetAddressOf()));
        auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
        properties.dpiX = properties.dpiY = 96;
        check(renderer.factory_->CreateWicBitmapRenderTarget(output.Get(), properties, renderer.target_.GetAddressOf()));
        check(renderer.target_->CreateSolidColorBrush(D2D1::ColorF(0), renderer.brush_.GetAddressOf()));
        Map map(6, 3);
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 6; ++x)
            map.at({x,y}).surface = x < 3 ? Surface::ShallowWater : Surface::DeepWater;
        const auto draw = [&](unsigned background, WorldView waterView = {{192,0},1}) {
            renderer.target_->BeginDraw(); renderer.target_->Clear(D2D1::ColorF(background));
            for (int y = 0; y < 3; ++y) for (int x = 0; x < 6; ++x) renderer.waterTile(map, {x,y}, waterView);
            check(renderer.target_->EndDraw());
            std::vector<UINT32> pixels(width * height);
            check(output->CopyPixels(nullptr, width * 4, UINT(pixels.size() * 4), reinterpret_cast<BYTE*>(pixels.data())));
            return pixels;
        };
        const WorldView waterView{{192,0},1};
        const auto pixel = [&](const auto& pixels, Vec2 world, const WorldView& view) {
            const auto p=view.project(world);
            return pixels[int(p.y)*width+int(p.x)];
        };
        const auto dark = draw(0), light = draw(0xffffff);
        require((pixel(light,{1.5f,1.5f},waterView)&255)-(pixel(dark,{1.5f,1.5f},waterView)&255)>100,
            "Shallow water hid the bed texture");
        require((pixel(light,{5,1.5f},waterView)&255)-(pixel(dark,{5,1.5f},waterView)&255)<12,
            "Deep water failed to attenuate the bed");
        for(int step=0;step<64;++step) {
            const float x=2.5f+step/64.0f;
            require(std::abs(int(pixel(light,{x,1.5f},waterView)&255)-int(pixel(light,{x+1/64.0f,1.5f},waterView)&255))<7,
                "Water gradient has a hard diamond seam");
        }
        const WorldView fractionalView{{192.3f,.7f},1.15f};
        const auto fractional=draw(0xffffff,fractionalView);
        for(int step=0;step<32;++step) {
            const float x=4.8f+step/80.0f;
            require(std::abs(int(pixel(fractional,{x,1.5f},fractionalView)&255)-int(pixel(fractional,{4.8f,1.5f},fractionalView)&255))<=1,
                "Fractional zoom exposed a seam between transparent diamonds");
        }
        map.at({4,1}).surface=Surface::ShallowWater;
        const auto edited=draw(0xffffff);
        require(pixel(edited,{4.5f,1.5f},waterView)!=pixel(light,{4.5f,1.5f},waterView),"Water cache ignored an edited depth");
        renderer.discardTarget();
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        auto scene = loadScenario(context.assets / "maps/sunny-hills.rtsmap", context.worldAssets, context.hallFootprint);
        Simulation game(std::move(scene), {}, definitions.entity("human.worker"), definitions.entities());
        game.revealMap();
        WorldView view{{}, 1.15f};
        view.origin = Vec2{700, 360} - view.project({41.5f,21.5f}, -1);
        renderer.snapshot(game, root / "water-preview.png", nullptr, false, nullptr, MenuPage::BattleSetup, 0, nullptr, &view);
    }
    static void occlusion(const tests::TestContext& context) {
        using render::check;
        using tests::require;
        platform::ComApartment apartment;
        Renderer renderer(Paths(context.assets, Paths::executable().parent_path() / "occlusion-test-data"));
        Renderer::ComPtr<IWICBitmap> output;
        constexpr UINT width = 240, height = 180;
        renderer.offscreenSize_ = {float(width), float(height)};
        check(renderer.wic_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, output.GetAddressOf()));
        auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
        properties.dpiX = properties.dpiY = 96;
        check(renderer.factory_->CreateWicBitmapRenderTarget(output.Get(), properties, renderer.target_.GetAddressOf()));
        check(renderer.target_->CreateSolidColorBrush(D2D1::ColorF(0xffffff), renderer.brush_.GetAddressOf()));
        auto scene = tests::flatScenario(); scene.worker = {3, 0};
        Simulation game(std::move(scene));
        WorldView view{{}, 1};
        const auto body = unitBounds(game, game.worker(), view);
        view.origin = Vec2{120, 90} - Vec2{body.x + body.width * .5f, body.y + body.height * .5f};
        const auto window = unitOcclusion(game, game.worker(), view);
        const auto draw = [&](float depth) {
            renderer.target_->BeginDraw(); renderer.target_->Clear(D2D1::ColorF(0));
            const bool masked = renderer.beginUnitOcclusion(game, view, depth, {0, 0, float(width), float(height)});
            renderer.target_->FillRectangle(D2D1::RectF(0, 0, float(width), float(height)), renderer.brush_.Get());
            if (masked) renderer.target_->PopLayer();
            check(renderer.target_->EndDraw());
            std::vector<UINT32> pixels(width * height);
            check(output->CopyPixels(nullptr, width * 4, UINT(pixels.size() * 4), reinterpret_cast<BYTE*>(pixels.data())));
            return pixels;
        };
        const auto pixels = draw(buildingDepth(game.buildings()[0]));
        const auto sample = [&](int x, int y) { return pixels[y * width + x] & 255; };
        require(sample(120, 90) >= 44 && sample(120, 90) <= 48, "Occlusion layer did not expose the rear image");
        const auto fringe = sample(int(window.body.x - window.padding - window.feather * .5f), 90);
        require(fringe > 70 && fringe < 230, "Occlusion fringe has a hard edge");
        require(sample(1, 1) == 255 && sample(238, 178) == 255, "Clamped mask changed pixels outside the window");
        const auto behind = draw(unitDrawDepth(game.worker().position));
        require((behind[90 * width + 120] & 255) == 255, "Background object was made transparent");
    }
    static void healthBars(const tests::TestContext& context) {
        using render::check;
        using tests::require;
        platform::ComApartment apartment;
        Renderer renderer(Paths(context.assets, Paths::executable().parent_path() / "health-bar-data"));
        Renderer::ComPtr<IWICBitmap> output;
        constexpr UINT width = 180, height = 100;
        check(renderer.wic_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, output.GetAddressOf()));
        auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
        properties.dpiX = properties.dpiY = 96;
        check(renderer.factory_->CreateWicBitmapRenderTarget(output.Get(), properties, renderer.target_.GetAddressOf()));
        check(renderer.target_->CreateSolidColorBrush(D2D1::ColorF(0), renderer.brush_.GetAddressOf()));
        renderer.target_->BeginDraw(); renderer.target_->Clear(D2D1::ColorF(0x253038));
        renderer.nightActive_ = true; // Health feedback must retain its UI brightness at night.
        Unit unit;
        unit.definition.maximumHealth = unit.health = 150;
        renderer.drawHealthBar(unit, {10, 10, 150, 6}, 100);
        unit.definition.maximumHealth = unit.health = 90;
        renderer.drawHealthBar(unit, {10, 25, 150, 6}, 100);
        unit.definition.maximumHealth = unit.health = 300;
        renderer.drawHealthBar(unit, {10, 40, 150, 6}, 100);
        unit.definition.maximumHealth = 150; unit.health = 20;
        unit.healthFeedback.record(150, 20, 100);
        renderer.drawHealthBar(unit, {10, 55, 150, 6}, 100, 0xe86464);
        renderer.drawHealthBar(unit, {10, 70, 150, 6}, 100 + HealthFeedback::holdTicks + HealthFeedback::drainTicks);
        unit.health = 0; unit.healthFeedback.record(20, 0, 101);
        renderer.drawHealthBar(unit, {10, 85, 150, 6}, 101);
        check(renderer.target_->EndDraw());
        std::vector<std::uint32_t> pixels(width * height);
        check(output->CopyPixels(nullptr, width * 4, UINT(pixels.size() * 4), reinterpret_cast<BYTE*>(pixels.data())));
        const auto at = [&](int x, int y) { return pixels[y * width + x]; };
        require(at(110, 12) == 0xff080f14 && at(109, 12) == 0xff75dc91 && at(111, 12) == 0xff75dc91,
            "150 HP did not put a black divider at the 100-HP boundary");
        for (int x = 10; x < 160; ++x) require(at(x, 27) == 0xff75dc91, "A bar below 100 HP acquired a divider");
        require(at(60, 42) == 0xff080f14 && at(110, 42) == 0xff080f14 && at(159, 42) == 0xff75dc91,
            "300 HP lost a section or gained a divider at its end");
        require(at(15, 57) == 0xff75dc91 && at(60, 57) == 0xffec4b48 && at(9, 57) == 0xffe86464,
            "Low health changed colour, damage was not red or enemy ownership disappeared");
        require(at(15, 72) == 0xff75dc91 && at(60, 72) == 0xff080f14, "Drained damage did not expose black behind green health");
        require(at(10, 87) == 0xff253038 && at(100, 87) == 0xff253038, "A dead unit retained its health bar");
    }
    static std::uint32_t snapshotPixel(Renderer& renderer, const std::filesystem::path& file, int x, int y) {
        using render::check;
        Renderer::ComPtr<IWICBitmapDecoder> decoder;
        Renderer::ComPtr<IWICBitmapFrameDecode> frame;
        Renderer::ComPtr<IWICFormatConverter> converter;
        check(renderer.wic_->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
        check(decoder->GetFrame(0, frame.GetAddressOf()));
        check(renderer.wic_->CreateFormatConverter(converter.GetAddressOf()));
        check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        std::uint32_t pixel{};
        const WICRect area{x, y, 1, 1};
        check(converter->CopyPixels(&area, 4, 4, reinterpret_cast<BYTE*>(&pixel)));
        return pixel;
    }
    static void composite(const tests::TestContext& context) {
        using namespace render;
        using tests::require;
        platform::ComApartment apartment;
        Renderer renderer(Paths(context.assets, Paths::executable().parent_path() / "lighting-test-data"));
        Renderer::ComPtr<IWICBitmap> output;
        check(renderer.wic_->CreateBitmap(4, 4, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, output.GetAddressOf()));
        auto targetProperties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
        targetProperties.dpiX = targetProperties.dpiY = 96;
        check(renderer.factory_->CreateWicBitmapRenderTarget(output.Get(), targetProperties, renderer.target_.GetAddressOf()));
        const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        const std::array<std::uint32_t, 4> pixels{0xffcc8844, 0x80664422, 0, 0xffcc8844};
        Renderer::ComPtr<ID2D1Bitmap> source, emission;
        check(renderer.target_->CreateBitmap(D2D1::SizeU(4, 1), pixels.data(), 16, properties, source.GetAddressOf()));
        renderer.prepareSpriteLighting(source.Get(), {reinterpret_cast<const BYTE*>(pixels.data()), 16}, 4, 1, false);
        const std::array<std::uint32_t, 4> mask{0, 0x80808080, 0xffffffff, 0xffffffff};
        check(renderer.target_->CreateBitmap(D2D1::SizeU(4, 1), mask.data(), 16, properties, emission.GetAddressOf()));
        const std::uint32_t night = 0x80102030;
        check(renderer.target_->CreateBitmap(D2D1::SizeU(1, 1), &night, 4, properties, renderer.nightBitmap_.GetAddressOf()));
        check(renderer.target_->CreateBitmapBrush(renderer.nightBitmap_.Get(), renderer.nightBrush_.GetAddressOf()));
        check(renderer.target_->CreateLayer(renderer.spriteLightLayer_.GetAddressOf()));
        renderer.target_->BeginDraw();
        renderer.target_->Clear(D2D1::ColorF(0, 0.0f));
        renderer.nightActive_ = true;
        renderer.sprite(source.Get(), rect(0, 0, 4, 1), {0, 0}, {4, 1}, true);
        renderer.sprite(source.Get(), rect(0, 0, 4, 1), {0, 1}, {4, 1}, true, 1, emission.Get());
        renderer.sprite(source.Get(), rect(0, 0, 4, 1), {0, 2}, {4, 1}, true, 0);
        renderer.nightActive_ = false;
        renderer.sprite(source.Get(), rect(0, 0, 4, 1), {0, 3}, {4, 1}, true);
        check(renderer.target_->EndDraw());
        std::array<std::uint32_t, 16> actual{};
        check(output->CopyPixels(nullptr, 16, sizeof(actual), reinterpret_cast<BYTE*>(actual.data())));
        require((actual[1] >> 24) == 128 && actual[2] == 0 && actual[6] == 0, "Night lighting changed sprite alpha or lit transparent pixels");
        require(actual[4] == actual[0] && actual[7] == pixels[3], "Black/white emission mask did not select unlit/original colour");
        for (int channel : {0, 8, 16}) {
            const int expected = (int((actual[1] >> channel) & 255) + int((pixels[1] >> channel) & 255)) / 2;
            require(std::abs(int((actual[5] >> channel) & 255) - expected) <= 2, "Half-transparent emission mask did not blend halfway");
        }
        for (size_t i = 0; i < 4; ++i)
            require(actual[8 + i] == pixels[i] && actual[12 + i] == pixels[i], "Emissive sprite or daylight lost original colour/alpha");
        // A foreground roof must cover the emitter after its night composition.
        Renderer::ComPtr<ID2D1SolidColorBrush> roof;
        check(renderer.target_->CreateSolidColorBrush(D2D1::ColorF(0x334455), roof.GetAddressOf()));
        renderer.target_->BeginDraw();
        renderer.target_->FillRectangle(rect(3, 1, 1, 1), roof.Get());
        check(renderer.target_->EndDraw());
        check(output->CopyPixels(nullptr, 16, sizeof(actual), reinterpret_cast<BYTE*>(actual.data())));
        require(actual[7] == 0xff334455, "Emission was drawn through foreground art");
    }
};
namespace tests {
void lightingTests(TestSuite& test, const TestContext& context) {
    test("Cached grid contours preserve ramps, camera transforms and one-pixel strokes", [&] { RendererLightingTest::gridContours(context); });
    test("Grid shares flat edges but retains cliffs, ramps and viewport boundaries", [&] {
        Map map(4, 4);
        const WorldView view{{400, 100}, 1};
        const Vec2 extent{1000, 700};
        int edges = 0;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) for (size_t edge = 0; edge < 4; ++edge)
            edges += render::terrainGridEdge(map, {x, y}, edge, view, extent, nullptr);
        require(edges == 40, "Flat grid repeats or loses a shared boundary");
        map.at({2, 1}).height = 1;
        require(render::terrainGridEdge(map, {1, 1}, 1, view, extent, nullptr), "Grid lost a cliff edge");
        map.at({2, 1}).height = 0; map.at({2, 1}).ramp = {0, 1};
        require(render::terrainGridEdge(map, {1, 1}, 1, view, extent, nullptr), "Grid lost a ramp edge");
        map.at({2, 1}).ramp = {};
        require(render::terrainGridEdge(map, {1, 1}, 1, {{1240, 100}, 1}, extent, nullptr), "Grid deferred an edge to a culled tile");
    });
    test("Joined grid strokes retain fog samples and refresh after exploration changes", [&] {
        Map map(12, 12);
        FogOfWar fog(12, 12); FogMask mask;
        const std::array sources{VisionSource{{5, 5}, 3}};
        fog.update(map, sources); mask.update(fog, 12, 12);
        const auto checkSamples = [&] {
            for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) {
                const Vec2 a{float(x), float(y)}, b{float(x + 1), float(y)};
                std::array<float, 4> actual{};
                render::boundaryRuns(a, b, &mask, [&](float start, float end, float light) {
                    for (int i = int(start * 4); i < int(end * 4); ++i) actual[i] = light;
                });
                for (int i = 0; i < 4; ++i)
                    require(actual[i] == mask.lightAt(a + (b - a) * ((i + .5f) / 4)), "Merged grid stroke exposed or brightened fog");
            }
        };
        checkSamples();
        fog.update(map, {}); mask.update(fog, 12, 12); checkSamples();
        fog.revealAll(); mask.update(fog, 12, 12); checkSamples();
        int runs = 0;
        render::boundaryRuns({5, 5}, {6, 5}, &mask, [&](float start, float end, float light) {
            ++runs; require(start == 0 && end == 1 && light == 1, "Fully lit boundary was not joined");
        });
        require(runs == 1, "Fully lit grid retained four separate draw calls");
        FogOfWar dark(12, 12); mask.update(dark, 12, 12);
        render::boundaryRuns({5, 5}, {6, 5}, &mask, [&](float, float, float) { require(false, "Grid draws through black mask after map replacement"); });
    });
    test("Water renders a translucent bed and smooth editable depth boundary", [&] { RendererLightingTest::water(context); });
    test("Occlusion layer exposes units with a soft local window and preserves background objects", [&] { RendererLightingTest::occlusion(context); });
    test("Health sections, damage colour and death visibility stay readable at night", [&] { RendererLightingTest::healthBars(context); });
    test("Night sprite composition preserves alpha, soft emission and foreground occlusion", [&] { RendererLightingTest::composite(context); });
    test("Night, dawn and daylight render with unchanged resources and paused time", [&] {
        platform::ComApartment apartment;
        const auto root = Paths::executable().parent_path();
        Renderer renderer(Paths(context.assets, root / "lighting-test-data"));
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        renderer.validateCombatAssets(definitions);
        Scenario site{Map(28, 28), {8, 11}, {11, 11}, {{{12, 12}, 1000}, {{13, 13}, 1000}}};
        for (const auto& crystal : site.crystals) site.map.occupy(crystal.cell);
        Simulation game(site, {}, definitions.entity("human.worker"), definitions.entities(), {}, {}, {.startMinute = 22 * 60});
        renderer.snapshot(game, root / "lighting-night.png", nullptr, true);
        WorldView shot{{},.85f}; shot.origin=Vec2{720,350}-shot.project(center(game.hall())+Vec2{1,2});
        const auto hidden=shot.project(center({2,18}));
        require(!game.fog().explored({2,18}),"Dark-pixel probe is no longer unexplored");
        const auto dark = RendererLightingTest::snapshotPixel(renderer, root / "lighting-night.png", int(hidden.x), int(hidden.y));
        // The fog's filtered edge and byte rounding may differ from the background by a couple of levels.
        for (const int shift : {0, 8, 16})
            require(std::abs(int((dark >> shift) & 255) - int((0x081119u >> shift) & 255)) <= 2,
                "Ambient light tinted unexplored terrain and exposed the fog tile boundary");
        require(game.clock().elapsedTicks() == 0 && game.crystals()[0].remaining == 1000, "Lighting advanced simulation");
        ticks(game, 4800);
        renderer.snapshot(game, root / "lighting-dawn.png");
        require(std::abs(nightStrength(game.clock()) - .5f) < .001f, "Dawn preview time changed");
        ticks(game, 3600);
        renderer.snapshot(game, root / "lighting-day.png");
        require(nightStrength(game.clock()) == 0, "Day preview still has night lighting");
    });
    test("Night battle remains renderable with both armies at minimum zoom", [&] {
        platform::ComApartment apartment;
        const auto root = Paths::executable().parent_path();
        Renderer renderer(Paths(context.assets, root / "lighting-test-data"));
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        renderer.validateCombatAssets(definitions);
        Scenario site{Map(32, 26), {8, 11}, {11, 11}, {{{12, 12}, 1000}}};
        site.map.occupy({12, 12});
        for (int y = 8; y < 16; ++y) for (int x = 15; x < 23; ++x)
            site.units.push_back({(x == 15 || x == 22) ? "human.slinger" : "human.peacemaker", static_cast<PlayerId>(x < 19 ? 0 : 1), {x, y}});
        auto types = definitions.entities();
        for (auto& type : types) type.dayVision = type.nightVision = 20;
        auto worker = definitions.entity("human.worker"); worker.dayVision = worker.nightVision = 20;
        Simulation game(std::move(site), {}, worker, types, {}, {}, {.startMinute = 22 * 60});
        GameplayUi ui;
        for (const auto& unit : game.units()) if (unit.owner == game.player().id) ui.selection.ids.push_back(unit.id);
        ticks(game, 8);
        require(game.units().size() > 40, "Battle lighting fixture lost its armies");
        for (const float zoom : {.85f, .35f}) {
            WorldView view{{0, 0}, zoom};
            view.origin = Vec2{720, 340} - view.project({16, 12}, 0);
            renderer.snapshot(game, root / (zoom < .5f ? "lighting-battle-far.png" : "lighting-battle.png"),
                nullptr, false, &ui, MenuPage::BattleSetup, 0, nullptr, &view);
        }
    });
}
}
}
