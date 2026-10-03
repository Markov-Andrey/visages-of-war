#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::drawPortraitBackdrop(UiRect bounds, unsigned color) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const auto area = rect(bounds.x, bounds.y, bounds.width, bounds.height);
    brush_->SetColor(D2D1::ColorF(0x000000));
    target_->FillRectangle(area, brush_.Get());

    auto& gradient = portraitGradients_[color];
    if (!gradient) {
        // Keep the black team's center visible against the black backing.
        const unsigned tint = color ? color : 0x303038;
        const D2D1_GRADIENT_STOP stops[]{
            {0.0f, D2D1::ColorF(tint, .75f)},
            {0.5f, D2D1::ColorF(tint, .75f)},
            {0.9f, D2D1::ColorF(tint, 0.0f)},
            {1.0f, D2D1::ColorF(tint, 0.0f)}
        };
        ComPtr<ID2D1GradientStopCollection> collection;
        check(target_->CreateGradientStopCollection(stops, UINT(std::size(stops)), D2D1_GAMMA_2_2,
            D2D1_EXTEND_MODE_CLAMP, collection.GetAddressOf()));
        check(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(), D2D1::Point2F(), 1, 1), collection.Get(), gradient.GetAddressOf()));
    }
    gradient->SetCenter(D2D1::Point2F(bounds.x + bounds.width * .5f, bounds.y + bounds.height * .5f));
    gradient->SetRadiusX(bounds.width * .5f);
    gradient->SetRadiusY(bounds.height * .5f);
    target_->FillRectangle(area, gradient.Get());
}
}
