#include "RenderSupport.hpp"
#include "DirectionalSpriteAssets.hpp"

namespace rts {
using namespace render;
void Renderer::snapshotUnitDirections(const UnitSpriteDefinition& definition, const std::filesystem::path& output) {
    if (definition.directionRecipe.empty()) throw std::invalid_argument("Direction overview requires a synthesis recipe");
    discardTarget();
    constexpr float label = 132, cell = 168, header = 52, rowHeight = 192;
    constexpr UINT width = 1644, height = 1588;
    ComPtr<IWICBitmap> canvas;
    check(wic_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, canvas.GetAddressOf()));
    auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
    properties.dpiX = properties.dpiY = 96;
    check(factory_->CreateWicBitmapRenderTarget(canvas.Get(), properties, target_.GetAddressOf()));
    check(target_->CreateSolidColorBrush(D2D1::ColorF(0xffffff), brush_.GetAddressOf()));
    auto* atlas = unitBitmap(definition, teamColor_);
    const std::array<const wchar_t*, 9> columns{L"Stand", L"Walk 1", L"Walk 2", L"Walk 3", L"Walk 4", L"Attack 1", L"Attack 2", L"Attack 3", L"Attack 4"};
    const std::array<const wchar_t*, 8> rows{L"S / warp", L"SE / source", L"E / warp", L"NE / mirror", L"N / warp", L"NW / source", L"W / warp", L"SW / mirror"};
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0x202a31));
    for (int column = 0; column < 9; ++column)
        text(columns[column], rect(label + column * cell + 12, 15, cell - 12, 26), 0xf5e8cf);
    for (int row = 0; row < 8; ++row) {
        const float top = header + row * rowHeight;
        text(rows[row], rect(8, top + 75, label - 12, 40), row == 1 || row == 5 ? 0xffce70 : 0xc5d9e2);
        for (int column = 0; column < 9; ++column) {
            const float left = label + column * cell;
            brush_->SetColor(D2D1::ColorF((row + column) % 2 ? 0x34434a : 0x3b4b50));
            target_->FillRectangle(rect(left, top, cell, rowHeight), brush_.Get());
            const Vec2 ground{left + cell * definition.anchor.x, top + cell * definition.anchor.y + 8};
            line({ground.x - 8, ground.y}, {ground.x + 8, ground.y}, 0x74998c);
            line({ground.x, ground.y - 4}, {ground.x, ground.y + 4}, 0x74998c);
            sprite(atlas, rect(float(column * definition.frameWidth), float(row * definition.frameHeight),
                float(definition.frameWidth), float(definition.frameHeight)), {left, top + 8}, {cell, cell}, definition.pixelArt, 0);
        }
    }
    check(target_->EndDraw());
    writeSnapshot(canvas.Get(), output);
    // An unlabelled atlas also makes frame-by-frame/animated inspection easy.
    auto& pixels = directionalSheets_.at({definition.directionRecipe, teamColor_});
    ComPtr<IWICBitmap> raw;
    check(wic_->CreateBitmapFromMemory(UINT(pixels.width), UINT(pixels.height), GUID_WICPixelFormat32bppPBGRA,
        UINT(pixels.width * 4), UINT(pixels.bgra.size()), pixels.bgra.data(), raw.GetAddressOf()));
    auto atlasOutput = output; atlasOutput.replace_filename(output.stem().wstring() + L"-atlas.png");
    writeSnapshot(raw.Get(), atlasOutput);
    discardTarget();
}
}
