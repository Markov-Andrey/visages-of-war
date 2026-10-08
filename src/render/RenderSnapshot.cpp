#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::snapshot(const Simulation& game, const std::filesystem::path& output, const Definitions* menuDefinitions, bool grid,
    const GameplayUi* interfaceState, MenuPage menuPage, double previewSeconds, const MenuState* menuState, const WorldView* snapshotView, Vec2 snapshotSize) {
    discardTarget();
    offscreenSize_ = {std::clamp(std::floor(snapshotSize.x), 800.0f, 8192.0f), std::clamp(std::floor(snapshotSize.y), 600.0f, 8192.0f)};
    ComPtr<IWICBitmap> bitmap;
    check(wic_->CreateBitmap(UINT(offscreenSize_.x), UINT(offscreenSize_.y), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.GetAddressOf()));
    auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
    properties.dpiX = properties.dpiY = 96;
    check(factory_->CreateWicBitmapRenderTarget(bitmap.Get(), properties, target_.GetAddressOf()));
    loadResources();
    if (menuDefinitions) {
        MenuState menu = menuState ? *menuState : MenuState{};
        if (!menuState) {
            menu.page = menuPage;
            menu.librarySeconds = previewSeconds;
        }
        drawMenu(game, menu, *menuDefinitions, {-1, -1});
    } else {
        WorldView view{{0, 0}, .85f};
        view.origin = offscreenSize_ * .5f - Vec2{0, 100} - view.project(center(game.hall()) + Vec2{1, 2}, 0);
        if (snapshotView) view = *snapshotView;
        GameplayUi ui;
        ui.selection.ids = {game.buildings().front().id};
        if (!grid && game.hero()) ui.selection.hero(game);
        if (grid) ui.placement = "human.barracks";
        if (interfaceState) ui = *interfaceState;
        draw(game, view, grid ? std::optional<Cell>{game.worker().cell + Cell{3,1}} : std::nullopt, ui, grid, false);
    }
    writeSnapshot(bitmap.Get(), output);
    discardTarget();
    offscreenSize_ = {};
}

void Renderer::writeSnapshot(IWICBitmap* bitmap, const std::filesystem::path& output) {
    ComPtr<IWICStream> stream;
    check(wic_->CreateStream(stream.GetAddressOf()));
    check(stream->InitializeFromFilename(output.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    check(wic_->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()));
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr));
    check(frame->Initialize(nullptr));
    UINT width{}, height{}; check(bitmap->GetSize(&width, &height));
    check(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format));
    check(frame->WriteSource(bitmap, nullptr));
    check(frame->Commit());
    check(encoder->Commit());
}
}
