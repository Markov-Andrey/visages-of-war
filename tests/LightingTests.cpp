#include "TestSupport.hpp"
#include "render/RenderSupport.hpp"

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
        Scenario site{Map(32, 32), {12, 15}, {17, 14}, {}}; site.slots[0].startingCrystals = 1000;
        Simulation game(site, {}, definitions.entity("church.worker"), definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id);
        const std::array builders{game.worker().id};
        const auto corps = game.construct(builders, "church.barracks", {17, 10});
        require(corps.has_value(), "Construction rendering fixture failed");
        game.revealMap();
        WorldView view{{}, 1}; view.origin = Vec2{720, 345} - view.project({16, 14});
        GameplayUi ui;
        // Pixel comparisons must not depend on the placement notification's wall-clock expiry.
        ui.notification.text(game, GameplayNotification::Clock::now() - std::chrono::seconds(5));
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
        Simulation night(site, {}, definitions.entity("church.worker"), definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id, {}, {}, {.startMinute = 22 * 60});
        night.revealMap();
        const std::array nightBuilders{night.worker().id};
        require(night.construct(nightBuilders, "church.barracks", {17, 10}).has_value(), "Night construction fixture failed");
        for (const auto& building : night.buildings())
            const_cast<Building&>(building).constructionProgress = building.definition.constructionTicks * 66 / 100;
        view.zoom = 1; view.origin = {}; view.origin = Vec2{720, 345} - view.project({16, 14});
        renderer.draw(night, view, {}, ui, false, false);
        // draw() disables world lighting before drawing the HUD; the amount retains the scene setting.
        require(renderer.nightAmount_ > 0, "Night construction fixture did not enable night lighting");
        renderer.writeSnapshot(output.Get(), Paths::executable().parent_path() / L"construction-live-night.png");
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
        auto game = rts::tests::makeTestSimulation(Scenario{Map(32,32), {1,1}, {4,4}, {}});
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
        auto replacement = rts::tests::makeTestSimulation(Scenario{Map(32,32), {1,1}, {4,4}, {}});
        renderer.updateFogMask(replacement);
        require(tile(false) == tile(true), "Grid retained visibility from a previous map");
        renderer.discardTarget();
        require(renderer.terrainGrids_.empty(), "Discarded renderer retained grid patterns");
    }
};
namespace tests {
void lightingTests(TestSuite& test, const TestContext& context) {
    test("Continuous grid survives cell clipping, zoom changes, fog and map replacement", [&] { RendererLightingTest::gridPattern(context); });
    test("Construction restores progress, completes exactly and shares only artwork", [&] { RendererLightingTest::construction(context); });
}
}
}
