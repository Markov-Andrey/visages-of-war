#include "TestSupport.hpp"
#include "game/CameraController.hpp"
#include "rts/TerrainVisibility.hpp"
#include "rts/LoadingScreen.hpp"

namespace rts::tests {
namespace {
float distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
constexpr Vec2 screenCenter{720, 350};
void advance(game::CameraController& camera, WorldView& view, int frames, float dt, Vec2 direction = {}) {
    for (int i = 0; i < frames; ++i) camera.update(view, screenCenter, direction, dt);
}
}
void cameraTests(TestSuite& test) {
    using game::CameraController;
    test("Loading composition keeps UI inside the viewport and covers parallax corners", [] {
        for (Vec2 extent : {Vec2{800, 600}, Vec2{1440, 900}, Vec2{1920, 1080}, Vec2{3440, 1440}}) {
            const LoadingLayout ui(extent);
            require(ui.map.width == ui.map.height, "Loading minimap must stay square");
            for (UiRect area : {ui.map, ui.frame, ui.title, ui.bar, ui.caption, ui.ornament, ui.status})
                require(area.x >= 0 && area.y >= 0 && area.x + area.width <= extent.x &&
                    area.y + area.height <= extent.y, "Loading UI extends beyond viewport");
            require(ui.fill.x > ui.bar.x && ui.fill.x + ui.fill.width < ui.bar.x + ui.bar.width &&
                ui.fill.y > ui.bar.y && ui.fill.y + ui.fill.height < ui.bar.y + ui.bar.height,
                "Progress fill covers its frame");
            for (float x : {-1.f, 1.f}) for (float y : {-1.f, 1.f}) {
                const LoadingBackdropLayout background(extent, {x, y});
                const auto bounds = background.layer({0, 0, 1672, 941}, 1);
                require(bounds.x <= 0 && bounds.y <= 0 && bounds.x + bounds.width >= extent.x &&
                    bounds.y + bounds.height >= extent.y, "Parallax exposes an outer image edge");
            }
        }
    });
    test("Terrain diagonal clipping preserves every on-screen cell across cameras and zooms", [] {
        for (const Cell size : {Cell{1, 1}, Cell{37, 83}, Cell{144, 144}, Cell{512, 512}})
            for (float zoom : {.08f, .6f, 1.f, 2.f})
                for (float offset : {-12000.f, -250.f, 0.f, 713.25f, 14000.f}) {
                    const WorldView view{{offset, -800}, zoom};
                    for (int depth = -1; depth <= size.x + size.y; ++depth) {
                        const auto span = terrainColumns(view, size.x, size.y, depth, -250, 1690);
                        for (int x = span.begin; x < span.end; ++x)
                            require(x >= 0 && x < size.x && depth - x >= 0 && depth - x < size.y,
                                "Clipped diagonal contains an invalid tile");
                        for (int x = std::max(0, depth - size.y + 1); x < std::min(size.x, depth + 1); ++x) {
                            const auto p = view.project(center({x, depth - x}), float(x % 5 - 1));
                            if (p.x > -250 && p.x < 1690)
                                require(x >= span.begin && x < span.end, "Clipping removed visible terrain");
                        }
                    }
                }
    });
    test("Camera clamps to the rectangular crop with a nonzero projected origin", [] {
        const auto map=Map::rectangular(96,64);
        const Vec2 origin{0,58}, size{1440,638}, middle=origin+size*.5f;
        for(const Vec2 direction:{Vec2{-1,0},Vec2{1,0},Vec2{0,-1},Vec2{0,1}}) {
            WorldView view{{},.85f}; CameraController camera; camera.stop(view);
            camera.setBounds(view,origin,size,map.groundExtent(),map.groundMinimum());
            camera.focus(view,{56,56},middle,true);
            for(int i=0;i<600;++i) camera.update(view,middle,direction,1.0f/60);
            const auto a=view.origin+map.groundMinimum()*view.zoom,b=a+map.groundExtent()*view.zoom;
            const float margin=game::cameraTuning.borderMargin;
            if(direction.x<0) require(std::abs(a.x-origin.x-margin)<.02f,"Left crop edge scrolled out of bounds");
            if(direction.x>0) require(std::abs(b.x-origin.x-size.x+margin)<.02f,"Right crop edge scrolled out of bounds");
            if(direction.y<0) require(std::abs(a.y-origin.y-margin)<.02f,"Top crop edge scrolled out of bounds");
            if(direction.y>0) require(std::abs(b.y-origin.y-size.y+margin)<.02f,"Bottom crop edge scrolled out of bounds");
        }
    });
    test("Camera map borders stop scrolling without accumulated pressure or blocked tangential motion", [] {
        const Vec2 origin{0, 58}, size{1440, 638}, map{64, 48}, middle = origin + size * .5f;
        const float margin = game::cameraTuning.borderMargin;
        for (float zoom : {.6f, .85f, 2.0f}) for (Vec2 direction : {Vec2{-1, 0}, Vec2{1, 0}, Vec2{0, -1}, Vec2{0, 1},
            Vec2{-1, -1}, Vec2{1, -1}, Vec2{-1, 1}, Vec2{1, 1}}) {
            WorldView view{{}, zoom}; CameraController camera; camera.stop(view);
            camera.setBounds(view, origin, size, map * 64); camera.focus(view, map * .5f, middle, true);
            for (int i = 0; i < 1000; ++i) camera.update(view, middle, direction, 1.0f / 60);
            const auto top = view.origin, bottom = view.origin + map * (64 * view.zoom);
            if (direction.x < 0) require(std::abs(top.x - origin.x - margin) < .02f, "Left reserve is not screen-sized");
            if (direction.x > 0) require(std::abs(bottom.x - origin.x - size.x + margin) < .02f, "Right reserve is not screen-sized");
            if (direction.y < 0) require(std::abs(top.y - origin.y - margin) < .02f, "Top limit included the HUD");
            if (direction.y > 0) require(std::abs(bottom.y - origin.y - size.y + margin) < .02f, "Bottom limit included the HUD");
            const auto stopped = view.origin;
            for (int i = 0; i < 60; ++i) camera.update(view, middle, direction, 1.0f / 60);
            require(distance(stopped, view.origin) < .01f, "Camera drifted beyond the border");
            camera.update(view, middle, direction * -1, 1.0f / 60);
            if (direction.x) require((view.origin.x - stopped.x) * direction.x > 0, "Horizontal reverse retained outward pressure");
            if (direction.y) require((view.origin.y - stopped.y) * direction.y > 0, "Vertical reverse retained outward pressure");
        }
        WorldView view{{}, .85f}; CameraController camera; camera.stop(view);
        camera.setBounds(view, origin, size, map * 64); camera.focus(view, WorldView{}.unproject({0, 12*64}), middle, true);
        const auto before = view.origin;
        for (int i = 0; i < 60; ++i) camera.update(view, middle, {-1, 1}, 1.0f / 60);
        require(view.origin.x == before.x && view.origin.y < before.y - 100, "Hitting one border stopped scrolling along it");
    });
    test("Smooth zoom preserves the point under the cursor and respects both zoom limits", [] {
        WorldView view{{80, -130}, .85f}; CameraController camera; camera.stop(view);
        const Vec2 cursor{510, 270}, ground = view.unproject(cursor, 2);
        camera.zoom(view, cursor, 3);
        require(view.zoom == .85f, "Wheel changed zoom instantaneously");
        for (int i = 0; i < 90; ++i) {
            camera.update(view, screenCenter, {}, 1.0f / 60);
            require(distance(view.project(ground, 2), cursor) < .01f, "Cursor anchor drifted during zoom");
        }
        camera.zoom(view, cursor, 100); advance(camera, view, 90, 1.0f / 60);
        require(view.zoom == game::cameraTuning.maximumZoom, "Maximum zoom was not respected");
        camera.zoom(view, cursor, -100); advance(camera, view, 90, 1.0f / 60);
        require(view.zoom == game::cameraTuning.minimumZoom, "Minimum zoom was not respected");
        require(distance(view.project(ground, 2), cursor) < .02f, "Zoom limits lost the cursor anchor");
    });
}
}
