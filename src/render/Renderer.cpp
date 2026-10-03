#include "RenderSupport.hpp"

namespace rts {
using namespace render;
Renderer::Renderer(Paths paths) : paths_(std::move(paths)), worldAssets_(WorldAssets::load(paths_)) {
    check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()));
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf())));
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic_.GetAddressOf())));
    const auto format = [&](float size, DWRITE_FONT_WEIGHT weight, ComPtr<IDWriteTextFormat>& output) {
        check(writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"ru-RU", output.GetAddressOf()));
    };
    format(14.0f, DWRITE_FONT_WEIGHT_NORMAL, bodyFormat_);
    format(25.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, titleFormat_);
}

void Renderer::attach(HWND window) { window_ = window; }

void Renderer::ensureTarget() {
    if (target_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const float dpi = static_cast<float>(GetDpiForWindow(window_));
    auto properties = D2D1::RenderTargetProperties();
    properties.dpiX = properties.dpiY = dpi;
    check(factory_->CreateHwndRenderTarget(properties,
        D2D1::HwndRenderTargetProperties(window_, D2D1::SizeU(client.right, client.bottom)), windowTarget_.GetAddressOf()));
    target_ = windowTarget_;
    loadResources();
}

void Renderer::discardTarget() {
    commandIcons_.clear();
    materialResources_.clear(); worldSprites_.clear(); unitSheets_.clear(); unitUiImages_.clear(); maskedImages_.clear(); paintResources_.clear();
    fogBrush_.Reset(); fogBitmap_.Reset();
    nightActive_ = false;
    spriteLights_.clear(); emissionMasks_.clear(); spriteLightLayer_.Reset();
    nightBrush_.Reset(); nightBitmap_.Reset();
    glowBrushes_.clear(); glowVisibilityBrush_.Reset(); glowVisibility_.Reset();
    groundBrush_.Reset();
    menuBackground_.Reset(); menuForeground_.Reset(); logo_.Reset(); buttonFrame_.Reset();
    minimap_.Reset(); tree_.Reset(); enemy_.Reset(); worker_.Reset(); crystal_.Reset(); hall_.Reset(); brush_.Reset(); target_.Reset(); windowTarget_.Reset();
}

void Renderer::resize(unsigned width, unsigned height) {
    if (!windowTarget_ || width == 0 || height == 0) return;
    target_->SetDpi(static_cast<float>(GetDpiForWindow(window_)), static_cast<float>(GetDpiForWindow(window_)));
    if (FAILED(windowTarget_->Resize(D2D1::SizeU(width, height)))) discardTarget();
}

Vec2 Renderer::size() const {
    if (offscreenSize_.x > 0) return offscreenSize_;
    RECT r{}; GetClientRect(window_, &r);
    const float scale = 96.0f / GetDpiForWindow(window_);
    return {r.right * scale, r.bottom * scale};
}
}
