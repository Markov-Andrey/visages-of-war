#include "RenderSupport.hpp"
#include "rts/UnitAnimation.hpp"

namespace rts {
using namespace render;
void Renderer::environmentObject(const EnvironmentObject& object, const Map& map, const WorldView& view) {
    if (!object.active()) return;
    if (!object.definitionId.empty()) {
        const auto& definition = worldAssets_.object(object.definitionId);
        if (!definition.image.empty()) {
            worldSprite(definition, {object.origin.x + object.width * .5f, object.origin.y + object.height - .5f}, 1, 0, map, view);
            return;
        }
    }
    const auto p = view.project(object.kind == EnvironmentKind::Rock ?
        Vec2{object.origin.x + object.width * .5f, object.origin.y + object.height - .5f} : center(object.origin),
        float(map.at(object.origin).height));
    const auto shifted = [&](Vec2 offset) { return p + offset * view.zoom; };
    if (object.kind == EnvironmentKind::Tree) {
        sprite(tree_.Get(), rect(0, 0, 16, 16), shifted({-55, -100}), Vec2{110, 110} * view.zoom, true);
    } else if (object.kind == EnvironmentKind::Rock) {
        const std::array<Vec2, 5> rock{{shifted({-45, 2}), shifted({-30, -51}), shifted({15, -69}), shifted({55, -2}), shifted({20, 35})}};
        polygon(rock, 0x737772);
        const std::array<Vec2, 3> face{{rock[2], rock[3], rock[4]}};
        polygon(face, 0x454f4e);
        const std::array<Vec2, 3> light{{rock[0], rock[1], rock[2]}};
        polygon(light, 0x93958a);
    } else {
        const auto b = view.project(center(object.origin + Cell{2, 0}), float(map.at(object.origin).height));
        const float z = view.zoom;
        for (const auto base : {p, b}) {
            const std::array<Vec2, 4> pillar{{base + Vec2{-14, 5} * z, base + Vec2{-14, -89} * z,
                base + Vec2{14, -89} * z, base + Vec2{14, 5} * z}};
            polygon(pillar, 0x929688);
            for (int i = 1; i < 5; ++i) line(base + Vec2{-13, -i * 18.0f} * z, base + Vec2{13, -i * 18.0f} * z, 0x606e66);
        }
        const std::array<Vec2, 4> beam{{p + Vec2{-16, -103} * z, b + Vec2{16, -103} * z,
            b + Vec2{16, -79} * z, p + Vec2{-16, -79} * z}};
        polygon(beam, 0xa4a794);
        polygon(beam, 0x5b6b63, 1, false);
    }
}

void Renderer::buildingSprite(const Simulation& game, const Building& b, const WorldView& view, bool selected) {
    const auto bounds = buildingBounds(game, b, view);
    const auto& type = b.definition;
    const float z = view.zoom;
    const float h = float(game.map().at(b.origin).height);
    const auto p = view.project({b.origin.x + type.width * .5f, b.origin.y + type.height * .5f}, h);
    const std::array<Vec2, 4> footprint{{view.project({float(b.origin.x), float(b.origin.y)}, h),
        view.project({float(b.origin.x + type.width), float(b.origin.y)}, h),
        view.project({float(b.origin.x + type.width), float(b.origin.y + type.height)}, h),
        view.project({float(b.origin.x), float(b.origin.y + type.height)}, h)}};
    if (selected || !b.complete()) { polygon(footprint, teamColor_, .16f); polygon(footprint, teamColor_, 1, false); }
    if (!b.complete()) {
        const std::array<Vec2, 4> foundation{{p + Vec2{-50, -22} * z, p + Vec2{50, -22} * z, p + Vec2{65, 12} * z, p + Vec2{-65, 12} * z}};
        polygon(foundation, 0x928169);
        for (const auto base : footprint) {
            line(base, base + Vec2{0, -65} * z, 0xcbad7c, 4 * z);
            line(base + Vec2{-12, -40} * z, base + Vec2{12, -40} * z, 0x887155, 3 * z);
        }
        text(wide(type.displayName), rect(p.x - 65 * z, p.y - 80 * z, 150, 25), 0xe6d6b0);
    } else if (type.visual != EntityVisual::Tower) {
        sprite(hall_.Get(), rect(96, 104, 312, 344), {bounds.x, bounds.y}, {bounds.width, bounds.height});
        if (type.visual == EntityVisual::Barracks) {
            line(p + Vec2{-25, -85} * z, p + Vec2{25, -35} * z, 0xe5d8b1, 5 * z);
            line(p + Vec2{25, -85} * z, p + Vec2{-25, -35} * z, 0xe5d8b1, 5 * z);
        }
    } else {
        const std::array<Vec2, 4> tower{{p + Vec2{-24, 18} * z, p + Vec2{-24, -83} * z, p + Vec2{24, -83} * z, p + Vec2{24, 18} * z}};
        polygon(tower, 0x8a9185);
        for (int i = 0; i < 5; ++i) line(p + Vec2{-23, -i * 18.0f} * z, p + Vec2{23, -i * 18.0f} * z, 0x555e59, 2 * z);
        const std::array<Vec2, 3> roof{{p + Vec2{-45, -80} * z, p + Vec2{0, -125} * z, p + Vec2{45, -80} * z}};
        polygon(roof, teamColor_);
    }
    if (b.complete()) {
        line(p + Vec2{28, -105} * z, p + Vec2{28, -205} * z, 0xd9d0b2, 2);
        const std::array<Vec2, 3> flag{{p + Vec2{28, -205} * z, p + Vec2{66, -193} * z, p + Vec2{28, -180} * z}};
        polygon(flag, teamColor_);
    }
    if (selected || !b.complete()) {
        const float y = b.complete() ? bounds.y - 12 : p.y - 100 * z;
        brush_->SetColor(D2D1::ColorF(0x10232b));
        target_->FillRectangle(rect(p.x - 48, y, 96, 6), brush_.Get());
        const float progress = b.complete() ? float(b.health) / type.maximumHealth : float(b.constructionProgress) / type.constructionTicks;
        brush_->SetColor(D2D1::ColorF(b.complete() ? 0x75c8a6 : 0xe3be79));
        target_->FillRectangle(rect(p.x - 48, y, 96 * progress, 6), brush_.Get());
    }
}

void Renderer::unitSprite(const Simulation& game, const Unit& u, const WorldView& view, bool selected) {
    const auto p = view.project(u.position, game.unitHeight(u));
    const Cell groundCell{int(std::floor(u.position.x)), int(std::floor(u.position.y))};
    const auto shadow = view.project(u.position, game.map().surfaceHeight(groundCell, u.position));
    brush_->SetColor(D2D1::ColorF(0, .35f));
    target_->FillEllipse(D2D1::Ellipse(point(shadow), 16 * view.zoom, 7 * view.zoom), brush_.Get());
    if (u.hero) {
        brush_->SetColor(D2D1::ColorF(0xe0c276));
        target_->DrawEllipse(D2D1::Ellipse(point(p), 27 * view.zoom, 13 * view.zoom), brush_.Get(), 2);
        text(L"" + std::to_wstring(u.level()), rect(p.x + 27 * view.zoom, p.y - 57 * view.zoom, 38, 24), 0xe0c276);
    }
    if (selected) {
        if (airborne(u.definition.movement)) {
            line(p, shadow, 0x76d99b, 1.5f);
            brush_->SetColor(D2D1::ColorF(0x76d99b));
            target_->FillEllipse(D2D1::Ellipse(point(shadow), 2.5f * view.zoom, 2.5f * view.zoom), brush_.Get());
        }
        brush_->SetColor(D2D1::ColorF(0x91e4bb));
        target_->DrawEllipse(D2D1::Ellipse(point(p), 23 * view.zoom, 11 * view.zoom), brush_.Get(), 2);
    }
    const auto frame = unitFrame(u);
    unitImage(u.definition.sprite, frame.column, frame.row, p, view.zoom, u.owner == game.player().id ? teamColor_ : enemyColor_);
    if (u.cargo > 0) {
        brush_->SetColor(D2D1::ColorF(0xc49af0));
        target_->FillEllipse(D2D1::Ellipse(point(p + Vec2{16, -25} * view.zoom), 5 * view.zoom, 7 * view.zoom), brush_.Get());
    }
    if (selected || u.hero || u.owner != game.player().id || u.health < u.maximumHealth()) {
        line(p + Vec2{-21, -59} * view.zoom, p + Vec2{21, -59} * view.zoom, 0x1b342e, 4);
        line(p + Vec2{-21 + 42.0f * u.health / u.maximumHealth(), -59} * view.zoom, p + Vec2{-21, -59} * view.zoom,
            u.owner == game.player().id ? 0x83cfaa : 0xe06464, 3);
    }
}
}
