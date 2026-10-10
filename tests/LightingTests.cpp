#include "TestSupport.hpp"
#include "render/RenderSupport.hpp"
#include "render/TerrainGrid.hpp"
#include "rts/UnitOcclusion.hpp"

namespace rts {
struct RendererLightingTest {
    static void construction(const tests::TestContext& context) {
        using tests::require;
        using render::check;
        platform::ComApartment apartment;
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        Renderer renderer(Paths(context.assets, Paths::executable().parent_path() / "construction-test-data"));
        renderer.offscreenSize_ = {1440, 900};
        Renderer::ComPtr<IWICBitmap> output;
        check(renderer.wic_->CreateBitmap(1440, 900, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, output.GetAddressOf()));
        auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
        properties.dpiX = properties.dpiY = 96;
        check(renderer.factory_->CreateWicBitmapRenderTarget(output.Get(), properties, renderer.target_.GetAddressOf()));
        renderer.loadResources(); renderer.validateCombatAssets(definitions);
        Scenario site{Map(32, 32), {12, 15}, {17, 14}, {}}; site.startingCrystals = 1000;
        Simulation game(site, {}, definitions.entity("human.worker"), definitions.entities());
        const std::array builders{game.worker().id};
        const auto corps = game.construct(builders, "human.barracks", {17, 10});
        require(corps.has_value(), "Construction rendering fixture failed");
        game.revealMap();
        WorldView view{{}, 1}; view.origin = Vec2{720, 345} - view.project({16, 14});
        GameplayUi ui;
        const auto read = [&] {
            std::vector<BYTE> pixels(1440 * 900 * 4);
            check(output->CopyPixels(nullptr, 1440 * 4, UINT(pixels.size()), pixels.data()));
            return pixels;
        };
        const auto set = [&](int percent) {
            for (const auto& b : game.buildings()) {
                auto& editable = const_cast<Building&>(b);
                editable.constructionProgress = b.definition.constructionTicks * percent / 100;
                editable.health = b.definition.maximumHealth;
            }
        };
        for (int percent : {0, 10, 33, 66, 99, 100}) {
            set(percent); renderer.draw(game, view, {}, ui, false, false);
            renderer.writeSnapshot(output.Get(), Paths::executable().parent_path() / (L"construction-live-" + std::to_wstring(percent) + L".png"));
        }
        const auto finished = read();
        for (const auto& b : game.buildings()) const_cast<Building&>(b).definition.buildingSprite.construction.reset();
        renderer.draw(game, view, {}, ui, false, false);
        require(read() == finished, "100% construction differs from the ordinary finished sprite");
        for (const auto& b : game.buildings()) const_cast<Building&>(b).definition.buildingSprite.construction = definitions.entity(b.definition.id).buildingSprite.construction;
        set(66); renderer.draw(game, view, {}, ui, false, false); const auto middle = read();
        renderer.draw(game, view, {}, ui, false, true);
        // Pause adds UI text; compare the world above the bottom interface.
        auto paused = read();
        require(std::equal(middle.begin() + 1440 * 100 * 4, middle.begin() + 1440 * 600 * 4, paused.begin() + 1440 * 100 * 4), "Paused construction changed world pixels");
        set(10); renderer.draw(game, view, {}, ui, false, false);
        set(66); renderer.draw(game, view, {}, ui, false, false);
        require(read() == middle, "Restoring construction progress changed the reveal mask");
        const auto& b = game.buildings().front();
        ui.constructionPreviews[b.id] = {.progress = .33f};
        renderer.draw(game, view, {}, ui, false, false); const auto independent = read();
        require(independent != middle, "Per-building visual progress had no effect");
        for (int y = 100; y < 600; ++y)
            require(std::equal(middle.begin() + (y * 1440 + 900) * 4, middle.begin() + (y * 1440 + 1400) * 4,
                independent.begin() + (y * 1440 + 900) * 4), "Hall preview changed the Corps construction progress");
        ui.constructionPreviews.clear(); renderer.draw(game, view, {}, ui, false, false);
        require(read() == middle && renderer.constructionResources_.size() == 2, "Shared cache retained per-instance progress");
        for (float zoom : {.5f, 1.5f}) {
            view.zoom = zoom; view.origin = {}; view.origin = Vec2{720, 345} - view.project({16,14});
            renderer.draw(game, view, {}, ui, false, false);
            renderer.writeSnapshot(output.Get(), Paths::executable().parent_path() / (zoom < 1 ? L"construction-live-far.png" : L"construction-live-near.png"));
        }
        Simulation night(site, {}, definitions.entity("human.worker"), definitions.entities(), {}, {}, {.startMinute = 22 * 60});
        night.revealMap();
        const std::array nightBuilders{night.worker().id};
        require(night.construct(nightBuilders, "human.barracks", {17, 10}).has_value(), "Night construction fixture failed");
        for (const auto& building : night.buildings())
            const_cast<Building&>(building).constructionProgress = building.definition.constructionTicks * 66 / 100;
        view.zoom = 1; view.origin = {}; view.origin = Vec2{720, 345} - view.project({16, 14});
        renderer.draw(night, view, {}, ui, false, false);
        // draw() disables world lighting before drawing the HUD; the amount retains the scene setting.
        require(renderer.nightAmount_ > 0, "Night construction fixture did not enable night lighting");
        renderer.writeSnapshot(output.Get(), Paths::executable().parent_path() / L"construction-live-night.png");
    }
    static void rallyOverlay(const tests::TestContext& context) {
        using render::check;
        using tests::require;
        platform::ComApartment apartment;
        Renderer renderer(Paths(context.assets, Paths::executable().parent_path() / "rally-test-data"));
        Renderer::ComPtr<IWICBitmap> output;
        constexpr UINT width = 800, height = 600;
        renderer.offscreenSize_ = {float(width), float(height)};
        check(renderer.wic_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, output.GetAddressOf()));
        auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
        properties.dpiX = properties.dpiY = 96;
        check(renderer.factory_->CreateWicBitmapRenderTarget(output.Get(), properties, renderer.target_.GetAddressOf()));
        renderer.loadResources(); // Use the solid fallback flag to probe exact overlay pixels.
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        Simulation game(Scenario{Map(24, 24), {8, 8}, {14, 14}, {}}, {},
            definitions.entity("human.worker"), definitions.entities());
        auto& building = const_cast<Building&>(game.buildings().front());
        building.rally = {8, 7};
        WorldView view{{}, 1};
        view.origin = Vec2{500, 250} - view.project(center(building.rally));
        const Vec2 probe{508, 206}; // Interior of the flag behind the hall in ground depth.
        require(buildingBounds(game, building, view).contains(probe), "Rally fixture misses the building sprite");
        const auto draw = [&](bool selected) {
            GameplayUi ui;
            if (selected) ui.selection.ids = {building.id};
            renderer.draw(game, view, {}, ui, false, false);
            std::uint32_t pixel{};
            const WICRect area{int(probe.x), int(probe.y), 1, 1};
            check(output->CopyPixels(&area, 4, 4, reinterpret_cast<BYTE*>(&pixel)));
            return pixel;
        };
        const int health = building.health;
        building.health = 0;
        const auto terrain = draw(false);
        building.health = health;
        const auto background = draw(false);
        require(background != terrain, "Rally fixture probe is not covered by building artwork");
        require(background != (0xff000000u | teamRgb(game.player().color)), "Rally fixture cannot distinguish the flag from the building");
        require(draw(true) == (0xff000000u | teamRgb(game.player().color)), "Building covered the rally marker");
        require(draw(false) == background, "Deselection left the overlay marker on screen");
    }
    static void gridPattern(const tests::TestContext& context) {
        using render::check;
        using tests::require;
        platform::ComApartment apartment;
        Renderer renderer(Paths(context.assets, Paths::executable().parent_path() / "grid-test-data"));
        Renderer::ComPtr<IWICBitmap> output;
        constexpr UINT width = 384, height = 320;
        check(renderer.wic_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, output.GetAddressOf()));
        auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
        properties.dpiX = properties.dpiY = 96;
        check(renderer.factory_->CreateWicBitmapRenderTarget(output.Get(), properties, renderer.target_.GetAddressOf()));
        check(renderer.target_->CreateSolidColorBrush(D2D1::ColorF(0), renderer.brush_.GetAddressOf()));
        const auto pixels = [&] {
            check(renderer.target_->EndDraw());
            std::vector<UINT32> result(width * height);
            check(output->CopyPixels(nullptr, width * 4, UINT(result.size() * 4), reinterpret_cast<BYTE*>(result.data())));
            return result;
        };
        Map map(4, 4);
        for (float zoom : {.08f, .35f, .85f, 2.f}) {
            const WorldView view{{192.35f, 17.6f}, zoom};
            const auto draw = [&](bool cells) {
                renderer.target_->BeginDraw(); renderer.target_->Clear(D2D1::ColorF(0));
                if (cells) {
                    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
                        renderer.terrainGrid({x,y}, {}, view, map.surfaceCorners({x,y}, view));
                } else {
                    renderer.target_->SetTransform(render::surfaceTransform(map.surfaceCorners({0,0}, view), {0,0}));
                    renderer.target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
                    renderer.target_->FillRectangle(render::rect(0, 0, 4, 4), renderer.terrainGridBrush({}, zoom));
                    renderer.target_->SetTransform(D2D1::Matrix3x2F::Identity());
                    renderer.target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                }
                return pixels();
            };
            const auto continuous = draw(false), tiled = draw(true);
            int difference = 0, lit = 0;
            for (size_t i = 0; i < tiled.size(); ++i) {
                difference += std::abs(int(tiled[i] & 255) - int(continuous[i] & 255));
                lit += (tiled[i] & 255) > 0;
            }
            require(lit > 20 && difference < 1500, "Clipping a continuous grid to cells introduced seams or duplicate edges");
            for (Cell ramp : {Cell{1,0}, Cell{-1,0}, Cell{0,1}, Cell{0,-1}}) renderer.terrainGridBrush(ramp, zoom);
            require(renderer.terrainGrids_.size() == 5, "Grid retained patterns from previous zoom levels");
        }
        const UINT32 ground = 0xff334433;
        Renderer::ComPtr<ID2D1Bitmap> bitmap;
        check(renderer.target_->CreateBitmap(D2D1::SizeU(1,1), &ground, 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),96,96), bitmap.GetAddressOf()));
        check(renderer.target_->CreateBitmapBrush(bitmap.Get(), renderer.groundBrush_.GetAddressOf()));
        Simulation game(Scenario{Map(32,32), {1,1}, {4,4}, {}});
        const Cell probe{24,24};
        WorldView view{{},1}; view.origin = Vec2{192,160} - view.project(center(probe));
        const auto tile = [&](bool grid) {
            renderer.target_->BeginDraw(); renderer.target_->Clear(D2D1::ColorF(0));
            renderer.tile(game.map(), probe, view, grid, true);
            return pixels();
        };
        renderer.updateFogMask(game);
        require(!game.fog().explored(probe) && tile(false) == tile(true), "Periodic grid leaked through unexplored fog");
        game.revealMap(); renderer.updateFogMask(game);
        require(tile(false) != tile(true), "Grid failed to appear after exploration changed");
        Simulation replacement(Scenario{Map(32,32), {1,1}, {4,4}, {}});
        renderer.updateFogMask(replacement);
        require(tile(false) == tile(true), "Grid retained visibility from a previous map");
        renderer.discardTarget();
        require(renderer.terrainGrids_.empty(), "Discarded renderer retained grid patterns");
    }
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
    test("Continuous grid survives cell clipping, zoom changes, fog and map replacement", [&] { RendererLightingTest::gridPattern(context); });
    test("Joined terrain boundary strokes retain fog samples and refresh after exploration changes", [&] {
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
    test("Construction restores progress, completes exactly and shares only artwork", [&] { RendererLightingTest::construction(context); });
    test("Construction phases and authored reveal order preserve exact endpoints", [&] {
        const auto start = constructionPhase(0), frame = constructionPhase(.33f), end = constructionPhase(1);
        require(start.invocation == 1 && start.lines == 0 && start.material == 0, "Invocation exposed the building");
        require(frame.invocation == 0 && frame.lines == 1 && frame.material == 0, "33 percent is not a pure light frame");
        require(end.invocation == 0 && end.material == 1 && end.glow == 0, "Finished building retained construction light");
        SpritePixels source{32, 24, std::vector<std::uint8_t>(32 * 24 * 4, 255)};
        auto order = source;
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 16; ++x)
            for (int c = 0; c < 3; ++c) order.bgra[(y * 32 + x) * 4 + c] = 0;
        const auto first = makeConstructionPixels(source, &source, &order);
        const auto second = makeConstructionPixels(source, &source, &order);
        require(first.regions == second.regions && first.lines.bgra == second.lines.bgra, "Construction mask generation is unstable");
        for (int i = 1; i < constructionBands - 1; ++i) require(first.regions[i].empty(), "Black/white reveal mask introduced unintended ranks");
        require(!first.regions[0].empty() && !first.regions.back().empty(), "Authored reveal order was ignored");
        for (const auto& r : first.regions[0]) require(r[0] + r[2] <= 16, "Dark-first reveal crossed the authored boundary");
        float area = 0;
        for (const auto& band : first.regions) for (const auto& r : band) area += r[2] * r[3];
        require(area == first.lines.width * first.lines.height, "Rank bands lost padding or overlap");
        GameplayUi::ConstructionPreview preview{0, true, 10};
        require(preview.at(10) == 0 && preview.at(10 + 15 * Simulation::ticksPerSecond) == 1, "Preview time is not tied to simulation ticks");
    });
    test("Rally marker overlays buildings and disappears on deselection", [&] { RendererLightingTest::rallyOverlay(context); });
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
