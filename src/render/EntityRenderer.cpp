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
        lightSurface(rock);
    } else {
        const auto b = view.project(center(object.origin + Cell{2, 0}), float(map.at(object.origin).height));
        const float z = view.zoom;
        for (const auto base : {p, b}) {
            const std::array<Vec2, 4> pillar{{base + Vec2{-14, 5} * z, base + Vec2{-14, -89} * z,
                base + Vec2{14, -89} * z, base + Vec2{14, 5} * z}};
            polygon(pillar, 0x929688);
            for (int i = 1; i < 5; ++i) line(base + Vec2{-13, -i * 18.0f} * z, base + Vec2{13, -i * 18.0f} * z, 0x606e66);
            lightSurface(pillar);
        }
        const std::array<Vec2, 4> beam{{p + Vec2{-16, -103} * z, b + Vec2{16, -103} * z,
            b + Vec2{16, -79} * z, p + Vec2{-16, -79} * z}};
        polygon(beam, 0xa4a794);
        polygon(beam, 0x5b6b63, 1, false);
        lightSurface(beam);
    }
}

void Renderer::buildingGroundSelection(const Simulation& game, const Building& b, const WorldView& view, int row) {
    if (row < b.origin.y || row >= b.origin.y + b.definition.height) return;
    const float height = float(game.map().at(b.origin).height);
    const auto a = view.project({float(b.origin.x), float(row)}, height);
    const auto c = view.project({float(b.origin.x + b.definition.width), float(row + 1)}, height);
    const auto color = selectionColor(b.owner, game.player().id);
    // Paint with the terrain row, before its objects. Only the outer footprint has a border.
    const auto antialias = target_->GetAntialiasMode();
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    brush_->SetColor(D2D1::ColorF(color, .16f));
    target_->FillRectangle(rect(a.x, a.y, c.x - a.x, c.y - a.y), brush_.Get());
    target_->SetAntialiasMode(antialias);
    line(a, {a.x, c.y}, color, 2);
    line({c.x, a.y}, c, color, 2);
    if (row == b.origin.y) line(a, {c.x, a.y}, color, 2);
    if (row + 1 == b.origin.y + b.definition.height) line({a.x, c.y}, c, color, 2);
}

void Renderer::buildingSprite(const Simulation& game, const Building& b, const WorldView& view, bool selected) {
    const auto bounds = buildingBounds(game, b, view);
    const auto& type = b.definition;
    const auto* stage = type.buildingSprite.stage(b.constructionProgress, type.constructionTicks);
    const auto color = b.owner == game.player().id ? teamColor_ : enemyColor_;
    const float z = view.zoom;
    const float h = float(game.map().at(b.origin).height);
    const auto p = view.project({b.origin.x + type.width * .5f, b.origin.y + type.height * .5f}, h);
    const std::array<Vec2, 4> footprint{{view.project({float(b.origin.x), float(b.origin.y)}, h),
        view.project({float(b.origin.x + type.width), float(b.origin.y)}, h),
        view.project({float(b.origin.x + type.width), float(b.origin.y + type.height)}, h),
        view.project({float(b.origin.x), float(b.origin.y + type.height)}, h)}};
    if (stage) {
        bool visible = false;
        for (int y = 0; y < type.height; ++y) for (int x = 0; x < type.width; ++x)
            visible |= game.fog().visible(b.origin + Cell{x, y});
        buildingImage(*stage, bounds, color, game.clock().elapsedTicks(), b.training(), visible);
    } else if (!b.complete()) {
        const std::array<Vec2, 4> foundation{{p + Vec2{-50, -22} * z, p + Vec2{50, -22} * z, p + Vec2{65, 12} * z, p + Vec2{-65, 12} * z}};
        polygon(foundation, 0x928169);
        lightSurface(foundation);
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
        lightSurface(tower);
        const std::array<Vec2, 3> roof{{p + Vec2{-45, -80} * z, p + Vec2{0, -125} * z, p + Vec2{45, -80} * z}};
        polygon(roof, color);
        lightSurface(roof);
    }
    if (b.complete() && !stage) {
        line(p + Vec2{28, -105} * z, p + Vec2{28, -205} * z, 0xd9d0b2, 2);
        const std::array<Vec2, 3> flag{{p + Vec2{28, -205} * z, p + Vec2{66, -193} * z, p + Vec2{28, -180} * z}};
        polygon(flag, color);
    } else if (stage && stage->teamMask.empty()) {
        // Ownership marker until the artist supplies a cloth mask for this stage.
        const Vec2 base{bounds.x + bounds.width - 12 * z, p.y + 12 * z};
        line(base, base + Vec2{0, -42} * z, 0xd9d0b2, 2 * z);
        const std::array<Vec2, 3> flag{{base + Vec2{0, -42} * z, base + Vec2{22, -35} * z, base + Vec2{0, -27} * z}};
        polygon(flag, color);
    }
    if (selected || !b.complete()) {
        const float y = (stage || b.complete()) ? bounds.y - 12 : p.y - 100 * z;
        drawHealthBar(b.health, type.maximumHealth, 0, {p.x - 48, y, 96, 6},
            selectionColor(b.owner, game.player().id), true);
    }
}

void Renderer::unitSprite(const Simulation& game, const Unit& u, const WorldView& view, bool selected) {
    const auto p = unitScreenAnchor(view, u.position, game.unitHeight(u));
    const Cell groundCell{int(std::floor(u.position.x)), int(std::floor(u.position.y))};
    const auto shadow = unitScreenAnchor(view, u.position, game.map().surfaceHeight(groundCell, u.position));
    brush_->SetColor(D2D1::ColorF(0, .35f));
    target_->FillEllipse(D2D1::Ellipse(point(shadow), 16 * view.zoom, 7 * view.zoom), brush_.Get());
    if (u.hero) {
        if (!selected) drawSelectionRing(p, 27, 13, view.zoom, 0xe0c276);
        text(L"" + std::to_wstring(u.level()), rect(p.x + 27 * view.zoom, p.y - 57 * view.zoom, 38, 24), 0xe0c276);
    }
    if (selected) {
        const auto color = selectionColor(u.owner, game.player().id);
        if (airborne(u.definition.movement)) {
            line(p, shadow, color, 1.5f);
            brush_->SetColor(D2D1::ColorF(color));
            target_->FillEllipse(D2D1::Ellipse(point(shadow), 2.5f * view.zoom, 2.5f * view.zoom), brush_.Get());
        }
        drawSelectionRing(p, u.hero ? 27 : 23, u.hero ? 13 : 11, view.zoom, color);
    }
    const auto frame = unitFrame(u);
    unitImage(u.definition.sprite, frame.column, frame.row, p, view.zoom, u.owner == game.player().id ? teamColor_ : enemyColor_);
    if (u.cargo > 0) {
        brush_->SetColor(D2D1::ColorF(worldAssets_.crystalSprite().glowColor));
        target_->FillEllipse(D2D1::Ellipse(point(p + Vec2{16, -25} * view.zoom), 5 * view.zoom, 7 * view.zoom), brush_.Get());
    }
    if (selected || u.hero || u.owner != game.player().id || u.health < u.maximumHealth()) {
        // Taller authored art needs its bar above the same bounds used for picking.
        const float barHeight = std::max(4.0f, 6 * view.zoom);
        const float barY = std::min(p.y - 59 * view.zoom, unitBounds(game, u, view).y - std::max(7 * view.zoom, barHeight * .5f + 2));
        drawHealthBar(u, {p.x - 21 * view.zoom, barY - barHeight * .5f, 42 * view.zoom, barHeight},
            game.clock().elapsedTicks(), selectionColor(u.owner, game.player().id), true);
    }
}
}
