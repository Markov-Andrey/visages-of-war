#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::buttonFrame(UiRect area) {
    if (!buttonFrame_) {
        brush_->SetColor(D2D1::ColorF(0xa6aaa8));
        target_->DrawRectangle(rect(area.x + .5f, area.y + .5f, area.width - 1, area.height - 1), brush_.Get());
        return;
    }
    const auto pixels = buttonFrame_->GetSize();
    sprite(buttonFrame_.Get(), rect(0, 0, pixels.width, pixels.height),
        {area.x, area.y}, {area.width, area.height});
}

void Renderer::polygon(std::span<const Vec2> points, unsigned color, float opacity, bool fill) {
    if (points.size() < 3) return;
    ComPtr<ID2D1PathGeometry> geometry;
    ComPtr<ID2D1GeometrySink> sink;
    check(factory_->CreatePathGeometry(geometry.GetAddressOf()));
    check(geometry->Open(sink.GetAddressOf()));
    sink->BeginFigure(point(points.front()), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < points.size(); ++i) sink->AddLine(point(points[i]));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    check(sink->Close());
    brush_->SetColor(D2D1::ColorF(color, opacity * worldOpacity_));
    if (fill) target_->FillGeometry(geometry.Get(), brush_.Get());
    else target_->DrawGeometry(geometry.Get(), brush_.Get(), 1.0f);
}

void Renderer::line(Vec2 a, Vec2 b, unsigned color, float width) {
    brush_->SetColor(D2D1::ColorF(color, worldOpacity_));
    target_->DrawLine(point(a), point(b), brush_.Get(), width);
}

void Renderer::text(const std::wstring& value, D2D1_RECT_F bounds, unsigned color, bool heading) {
    brush_->SetColor(D2D1::ColorF(color));
    target_->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), heading ? titleFormat_.Get() : bodyFormat_.Get(),
                      bounds, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Renderer::sprite(ID2D1Bitmap* bitmap, D2D1_RECT_F source, Vec2 topLeft, Vec2 extent, bool pixel, float lighting, ID2D1Bitmap* emission) {
    if (nightActive_ && lighting > 0 && spriteLights_.contains(bitmap)) {
        litSprite(bitmap, source, topLeft, extent, pixel, lighting, emission);
        return;
    }
    target_->DrawBitmap(bitmap, rect(topLeft.x, topLeft.y, extent.x, extent.y), worldOpacity_,
        pixel ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
}
}
