#include "RenderSupport.hpp"
#include <algorithm>

namespace rts {
using namespace render;
ID2D1Bitmap* Renderer::unitBitmap(const UnitSpriteDefinition& d, unsigned color) {
    const auto path = imagePath(d.image);
    if (d.teamMask == SpriteTeamMask::None) color = 0;
    auto& bitmap = unitSheets_[{path, color, d.teamMask}];
    if (!bitmap) loadBitmap(paths_.asset(path), bitmap, color, d.teamMask);
    return bitmap.Get();
}
void Renderer::unitImage(const UnitSpriteDefinition& d, int column, int row, Vec2 ground, float zoom, unsigned color) {
    const auto extent = d.size * zoom;
    sprite(unitBitmap(d, color), rect(float(column * d.frameWidth), float(row * d.frameHeight), float(d.frameWidth), float(d.frameHeight)),
        ground - Vec2{extent.x * d.anchor.x, extent.y * d.anchor.y}, extent, true);
}
void Renderer::unitPortrait(const UnitSpriteDefinition& d, Vec2 topLeft, Vec2 extent, unsigned color) {
    sprite(unitBitmap(d, color), rect(float(d.idle * d.frameWidth), float(d.rows[0] * d.frameHeight), float(d.frameWidth), float(d.frameHeight)), topLeft, extent, true);
}
void Renderer::validateCombatAssets(const Definitions& definitions) {
    std::map<std::filesystem::path,Vec2> sizes;
    const auto dimensions = [&](const std::string& image) {
        const auto path = paths_.asset(imagePath(image));
        if (const auto found = sizes.find(path); found != sizes.end()) return found->second;
        ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
        check(wic_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
        check(decoder->GetFrame(0, frame.GetAddressOf())); UINT w{}, h{}; check(frame->GetSize(&w, &h));
        if (!w || !h || w > 8192 || h > 8192) throw std::runtime_error("Invalid combat sprite dimensions: " + image);
        const Vec2 size{float(w),float(h)}; sizes.emplace(path,size); return size;
    };
    for (const auto& e : definitions.entities()) {
        if (e.mobile) {
            const auto& s = e.sprite; const auto size = dimensions(s.image);
            int column = s.idle;
            for (const auto* frames : {&s.walk, &s.windup, &s.recovery}) column = std::max(column, *std::max_element(frames->begin(), frames->end()));
            const int row = *std::max_element(s.rows.begin(), s.rows.end());
            if ((column + 1) * s.frameWidth > size.x || (row + 1) * s.frameHeight > size.y)
                throw std::runtime_error("Animation frame outside sprite sheet: " + e.id);
        }
        if (e.projectile) {
            const auto& p = *e.projectile; const auto size = dimensions(p.image);
            if (p.source[2] && (p.source[0] + p.source[2] > size.x || p.source[1] + p.source[3] > size.y))
                throw std::runtime_error("Projectile source outside image: " + e.id);
        }
    }
}
}
