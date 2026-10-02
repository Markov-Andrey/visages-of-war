#include "TestSupport.hpp"
#include "render/RenderSupport.hpp"

namespace rts {
struct RendererLightingTest {
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
    test("Night sprite composition preserves alpha, soft emission and foreground occlusion", [&] { RendererLightingTest::composite(context); });
    test("Night, dawn and daylight render with unchanged resources and paused time", [&] {
        platform::ComApartment apartment;
        const auto root = Paths::executable().parent_path();
        Renderer renderer(Paths(context.assets, root / "lighting-test-data"));
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        renderer.validateCombatAssets(definitions);
        Scenario site{Map(28, 28), {8, 11}, {11, 11}, {{{12, 12}, 1000}, {{13, 13}, 1000}}};
        for (const auto& crystal : site.crystals) site.map.occupy(crystal.cell);
        Simulation game(site, {}, definitions.entity("human.worker"), definitions.entities());
        renderer.snapshot(game, root / "lighting-night.png");
        const auto dark = RendererLightingTest::snapshotPixel(renderer, root / "lighting-night.png", 270, 400);
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
            site.units.push_back({(x == 15 || x == 22) ? "human.archer" : "human.soldier", static_cast<PlayerId>(x < 19 ? 0 : 1), {x, y}});
        auto types = definitions.entities();
        for (auto& type : types) type.dayVision = type.nightVision = 20;
        auto worker = definitions.entity("human.worker"); worker.dayVision = worker.nightVision = 20;
        Simulation game(std::move(site), {}, worker, types);
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
