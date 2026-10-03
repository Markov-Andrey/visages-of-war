#include "RenderSupport.hpp"
#include "DirectionalSpriteAssets.hpp"
#include "rts/UnitAnimation.hpp"
#include <algorithm>

namespace rts {
using namespace render;
ID2D1Bitmap* Renderer::unitBitmap(const UnitSpriteDefinition& d, unsigned color) {
    const auto path = imagePath(d.directionRecipe.empty() ? d.image : d.directionRecipe);
    if (d.teamMask == SpriteTeamMask::None) color = 0;
    auto& bitmap = unitSheets_[{path, color, d.teamMask}];
    if (!bitmap) {
        if (d.directionRecipe.empty()) loadBitmap(paths_.asset(path), bitmap, color, d.teamMask, {}, true);
        else {
            auto& pixels = directionalSheets_[d.directionRecipe];
            if (pixels.bgra.empty()) pixels = loadDirectionalSprite(paths_, wic_.Get(), d);
            const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            check(target_->CreateBitmap(D2D1::SizeU(UINT(pixels.width), UINT(pixels.height)), pixels.bgra.data(), UINT(pixels.width * 4), properties, bitmap.GetAddressOf()));
            prepareSpriteLighting(bitmap.Get(), pixels.bgra, UINT(pixels.width), UINT(pixels.height), false);
        }
    }
    return bitmap.Get();
}
void Renderer::unitImage(const UnitSpriteDefinition& d, int column, int row, Vec2 ground, float zoom, unsigned color) {
    const auto extent = d.size * zoom;
    sprite(unitBitmap(d, color), rect(float(column * d.frameWidth), float(row * d.frameHeight), float(d.frameWidth), float(d.frameHeight)),
        ground - Vec2{extent.x * d.anchor.x, extent.y * d.anchor.y}, extent, d.pixelArt, .5f);
}
void Renderer::unitPortrait(const UnitSpriteDefinition& d, Vec2 topLeft, Vec2 extent, unsigned color) {
    sprite(unitBitmap(d, color), rect(float(d.idle * d.frameWidth), float(d.rows[0] * d.frameHeight), float(d.frameWidth), float(d.frameHeight)), topLeft, extent, d.pixelArt);
}
void Renderer::unitHudPortrait(const UnitSpriteDefinition& d, UiRect bounds, unsigned color) {
    if (d.portrait.empty()) {
        unitPortrait(d, {bounds.x, bounds.y}, {bounds.width, bounds.height}, color);
        return;
    }
    const auto path = imagePath(d.portrait);
    auto& bitmap = unitPortraits_[path];
    if (!bitmap) loadBitmap(paths_.asset(path), bitmap);
    const auto pixels = bitmap->GetPixelSize();
    const float scale = std::min(bounds.width / pixels.width, bounds.height / pixels.height);
    const Vec2 extent{pixels.width * scale, pixels.height * scale};
    sprite(bitmap.Get(), rect(0, 0, float(pixels.width), float(pixels.height)),
        {bounds.x + (bounds.width - extent.x) * .5f, bounds.y + (bounds.height - extent.y) * .5f}, extent);
}
void Renderer::corpseSprite(const Corpse& corpse, const WorldView& view, unsigned color) {
    const auto ground = unitScreenAnchor(view, corpse.position, corpse.height);
    if (const auto* frame = corpseFrame(corpse)) {
        const auto& d = corpse.sprite;
        const auto& death = *d.death;
        const auto path = imagePath(death.image);
        if (d.teamMask == SpriteTeamMask::None) color = 0;
        auto& bitmap = unitSheets_[{path, color, d.teamMask}];
        if (!bitmap) loadBitmap(paths_.asset(path), bitmap, color, d.teamMask, {}, true);
        const auto& r = frame->source;
        const float scale = death.scale * view.zoom;
        sprite(bitmap.Get(), rect(float(r[0]), float(r[1]), float(r[2]), float(r[3])),
            ground - frame->anchor * scale, Vec2{float(r[2]), float(r[3])} * scale, d.pixelArt, .5f);
        return;
    }
    // Temporary remains for units whose dedicated death artwork is not supplied yet.
    D2D1_MATRIX_3X2_F transform;
    target_->GetTransform(&transform);
    target_->SetTransform(D2D1::Matrix3x2F::Rotation(90, point(ground)) * transform);
    unitPortrait(corpse.sprite, ground + Vec2{-32,-32} * view.zoom, Vec2{64,64} * view.zoom, color);
    target_->SetTransform(transform);
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
    std::map<std::string,RallySpriteDefinition> rallySprites;
    for (const auto& commander : definitions.commanders()) if (commander.rallySprite) {
        const auto& sprite = *commander.rallySprite;
        const auto size = dimensions(sprite.image);
        for (const auto& frame : sprite.frames)
            if (frame.source[0] + frame.source[2] > size.x || frame.source[1] + frame.source[3] > size.y)
                throw std::runtime_error("Rally frame outside image: " + commander.id);
        if (!sprite.teamMask.empty() && dimensions(sprite.teamMask) != size)
            throw std::runtime_error("Rally team mask dimensions must match image: " + commander.id);
        rallySprites.emplace(commander.id, sprite);
    }
    for (const auto& e : definitions.entities()) {
        for (const auto& stage : e.buildingSprite.stages) {
            const auto size = dimensions(stage.image);
            if (stage.source[0] + stage.source[2] > size.x || stage.source[1] + stage.source[3] > size.y)
                throw std::runtime_error("Building source outside image: " + e.id);
            if (!stage.teamMask.empty() && dimensions(stage.teamMask) != size)
                throw std::runtime_error("Building team mask dimensions must match image: " + e.id);
            if (!stage.emissionMask.empty() && dimensions(stage.emissionMask) != size)
                throw std::runtime_error("Building emission mask dimensions must match image: " + e.id);
            for (const auto& layer : stage.layers) {
                const auto layerSize = dimensions(layer.image);
                for (const auto& source : layer.frames)
                    if (source[0] + source[2] > layerSize.x || source[1] + source[3] > layerSize.y)
                        throw std::runtime_error("Building layer frame outside image: " + e.id);
                if (!layer.teamMask.empty() && dimensions(layer.teamMask) != layerSize)
                    throw std::runtime_error("Building layer team mask dimensions must match image: " + e.id);
                if (!layer.emissionMask.empty() && dimensions(layer.emissionMask) != layerSize)
                    throw std::runtime_error("Building layer emission mask dimensions must match image: " + e.id);
            }
        }
        if (e.mobile) {
            const auto& s = e.sprite; auto size = dimensions(s.image);
            if (!s.directionRecipe.empty()) {
                const auto recipe = directionalRecipe(paths_, s);
                for (const auto& direction : recipe.sources) for (const auto& frame : direction) {
                    const auto sourceSize = dimensions(frame.image);
                    if (frame.source[0] + frame.source[2] > sourceSize.x || frame.source[1] + frame.source[3] > sourceSize.y)
                        throw std::runtime_error("Directional frame outside image: " + e.id);
                }
                auto& pixels = directionalSheets_[s.directionRecipe];
                if (pixels.bgra.empty()) pixels = loadDirectionalSprite(paths_, wic_.Get(), s);
                size = {float(pixels.width), float(pixels.height)};
            }
            if (!s.portrait.empty()) dimensions(s.portrait);
            if (s.death) {
                const auto deathSize = dimensions(s.death->image);
                for (const auto& frame : s.death->frames)
                    if (frame.source[0] + frame.source[2] > deathSize.x || frame.source[1] + frame.source[3] > deathSize.y)
                        throw std::runtime_error("Death frame outside image: " + e.id);
            }
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
    rallySprites_ = std::move(rallySprites);
}
}
