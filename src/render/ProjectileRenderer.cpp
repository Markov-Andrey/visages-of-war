#include "RenderSupport.hpp"
#include <numbers>

namespace rts {
using namespace render;
void Renderer::drawProjectiles(const Simulation& game, const WorldView& view) {
    for (const auto& p : game.projectiles()) {
        if (!game.fog().visible({int(std::floor(p.position.x)), int(std::floor(p.position.y))})) continue;
        const auto& d = p.definition;
        ID2D1Bitmap* bitmap{};
        if (!d.teamMask.empty()) bitmap = maskedBitmap(d.image, d.teamMask, p.owner == game.player().id ? teamColor_ : enemyColor_);
        else {
            const auto path = imagePath(d.image);
            auto& original = worldSprites_[path]; if (!original) loadBitmap(paths_.asset(path), original);
            bitmap = original.Get();
        }
        const auto imageSize = bitmap->GetSize();
        const auto source = d.source[2] ? rect(float(d.source[0]),float(d.source[1]),float(d.source[2]),float(d.source[3])) : rect(0,0,imageSize.width,imageSize.height);
        const auto position = projectileScreenPosition(view, p);
        const auto tangent = projectileScreenTangent(view, p);
        const float rotation = std::atan2(tangent.y,tangent.x) * 180 / std::numbers::pi_v<float> + d.rotationOffset;
        D2D1_MATRIX_3X2_F previous; target_->GetTransform(&previous);
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(rotation,point(position)) * previous);
        const auto extent = d.size * view.zoom;
        sprite(bitmap,source,position - Vec2{extent.x * d.anchor.x, extent.y * d.anchor.y},extent,false,0);
        target_->SetTransform(previous);
    }
    for (const auto& hit : game.projectileImpacts()) {
        if (hit.radius <= 0 || !game.fog().visible({int(hit.position.x),int(hit.position.y)})) continue;
        const auto p = view.project(hit.position,hit.height);
        const float progress = 1 - hit.remainingTicks / 12.0f;
        brush_->SetColor(D2D1::ColorF(0xd8bc8a, (1 - progress) * .65f));
        const float radius = hit.radius * WorldView::tileSize / groundPlaneScale * view.zoom * (.3f + .7f * progress);
        target_->DrawEllipse(D2D1::Ellipse(point(p),radius,radius),brush_.Get(),2 * view.zoom);
    }
}
}
