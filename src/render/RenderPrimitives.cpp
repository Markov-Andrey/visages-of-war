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

void Renderer::commandHotkey(UiRect area, wchar_t key, bool enabled) {
    const auto square = rect(area.x + 2, area.y + 2, 18, 18);
    brush_->SetColor(D2D1::ColorF(0x101821, .84f));
    target_->FillRectangle(square, brush_.Get());
    brush_->SetColor(D2D1::ColorF(enabled ? 0xb9a47b : 0x687477, .95f));
    target_->DrawRectangle(square, brush_.Get(), 1);
    brush_->SetColor(D2D1::ColorF(enabled ? 0xf3e6c7 : 0xa6afb3));
    target_->DrawTextW(&key, 1, hotkeyFormat_.Get(), square, brush_.Get());
}

void Renderer::autocastFrame(UiRect area, AutocastState state) {
    const bool on = state != AutocastState::Off;
    const unsigned color = !on ? 0x71817f : state == AutocastState::Mixed ? 0xb7a886 : 0xf2c968;
    if (on) {
        const float x = area.x + 2, y = area.y + 2, w = area.width - 4, h = area.height - 4;
        brush_->SetColor(D2D1::ColorF(color));
        target_->DrawRectangle(rect(x, y, w, h), brush_.Get(), 2);
        // Presentation time keeps the indicator moving while the match is paused.
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const float perimeter = 2 * (w + h), head = float(std::fmod(seconds / 2.4, 1.0)) * perimeter;
        const auto at = [&](float distance) -> Vec2 {
            distance = std::fmod(distance + perimeter, perimeter);
            if (distance < w) return {x + distance, y};
            distance -= w;
            if (distance < h) return {x + w, y + distance};
            distance -= h;
            if (distance < w) return {x + w - distance, y + h};
            return {x, y + h - (distance - w)};
        };
        for (int i = 24; i >= 0; --i) {
            brush_->SetColor(D2D1::ColorF(0xfff5cc, 1.f - i / 25.f));
            target_->DrawLine(point(at(head - i)), point(at(head - i + .8f)), brush_.Get(), 3);
        }
    }
    const UiRect badge{area.x + area.width - 20, area.y + area.height - 20, 17, 17};
    brush_->SetColor(D2D1::ColorF(0x101821, .95f));
    target_->FillRectangle(rect(badge.x, badge.y, badge.width, badge.height), brush_.Get());
    brush_->SetColor(D2D1::ColorF(color));
    const wchar_t mark = state == AutocastState::Mixed ? L'±' : L'А';
    target_->DrawTextW(&mark, 1, hotkeyFormat_.Get(), rect(badge.x, badge.y, badge.width, badge.height), brush_.Get());
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

void Renderer::centeredText(const std::wstring& value, UiRect bounds, unsigned color, bool heading) {
    auto* format = heading ? titleFormat_.Get() : bodyFormat_.Get();
    const auto alignment = format->GetTextAlignment();
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    text(value, rect(bounds.x, bounds.y, bounds.width, bounds.height), color, heading);
    format->SetTextAlignment(alignment);
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
