#include "TestSupport.hpp"
#include "game/CameraController.hpp"

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
    test("Camera scrolling uses all client edges and ignores points outside the window", [] {
        const Vec2 extent{1440, 900};
        require(CameraController::edgeDirection({0, 450}, extent) == Vec2{-1, 0}, "Left edge failed");
        require(CameraController::edgeDirection({1439, 450}, extent) == Vec2{1, 0}, "Right edge failed");
        require(CameraController::edgeDirection({720, 0}, extent) == Vec2{0, -1}, "Top edge failed");
        require(CameraController::edgeDirection({720, 899}, extent) == Vec2{0, 1}, "Bottom edge failed");
        require(CameraController::edgeDirection({1439, 899}, extent) == Vec2{1, 1}, "Corner failed");
        for (const auto point : {Vec2{720, 450}, Vec2{-1, 450}, Vec2{1440, 450}, Vec2{700, 901}})
            require(CameraController::edgeDirection(point, extent) == Vec2{}, "Camera moved outside the edge band");
    });
    test("Camera focus flies continuously to the requested world point without overshooting", [] {
        WorldView view{{90, -120}, .85f}; CameraController camera; camera.stop(view);
        const Vec2 target{28, 19}, before = view.origin;
        camera.focus(view, target, screenCenter);
        require(view.origin == before, "Focus teleported before a frame");
        float previous = distance(view.project(target), screenCenter);
        const float initial = previous;
        for (int frame = 0; frame < 90; ++frame) {
            camera.update(view, screenCenter, {}, 1.0f / 60);
            const float error = distance(view.project(target), screenCenter);
            require(error <= previous + .002f, "Focus oscillated away from destination");
            if (frame == 0) require(error > initial * .9f && error < initial, "First focus frame jumped or did not move");
            if (frame == 23) require(error < initial * .02f, "Focus did not arrive promptly");
            previous = error;
        }
        require(distance(view.project(target), screenCenter) < .01f, "Focus ended at the wrong map point");
    });
    test("Camera pan accelerates and brakes with consistent diagonal speed and frame rate", [] {
        const auto trajectory = [](int fps, Vec2 direction) {
            WorldView view; CameraController camera; camera.stop(view);
            advance(camera, view, fps, 1.0f / fps, direction);
            const auto afterPan = view.origin;
            advance(camera, view, fps * 2, 1.0f / fps);
            require(distance(view.origin, afterPan) > 1, "Pan stopped abruptly without braking");
            const auto stopped = view.origin;
            advance(camera, view, fps, 1.0f / fps);
            require(distance(view.origin, stopped) < .01f, "Camera kept drifting at rest");
            return view.origin;
        };
        const auto low = trajectory(30, {1, 0}), high = trajectory(144, {1, 0});
        require(distance(low, high) < .05f, "Pan distance depends on frame rate");
        require(std::abs(distance(trajectory(60, {1, 1}), {}) - distance(trajectory(60, {1, 0}), {})) < .05f,
            "Diagonal camera scrolling was faster");
        WorldView view; CameraController camera; camera.stop(view);
        camera.update(view, screenCenter, {1, 0}, 1.0f / 60);
        const float first = -view.origin.x;
        camera.update(view, screenCenter, {1, 0}, 1.0f / 60);
        require(-view.origin.x - first > first, "Pan did not accelerate smoothly");
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
    test("Manual camera movement interrupts flights and stopping clears all residual motion", [] {
        WorldView view; CameraController camera; camera.stop(view);
        camera.focus(view, {35, 30}, screenCenter); advance(camera, view, 3, 1.0f / 60);
        const auto before = view.origin;
        advance(camera, view, 15, 1.0f / 60, {-1, 0});
        require(view.origin.x > before.x && std::abs(view.origin.y - before.y) < .001f, "Manual pan did not replace the flight");
        camera.zoom(view, {300, 300}, 4); advance(camera, view, 3, 1.0f / 60);
        camera.stop(view); const auto stopped = view;
        advance(camera, view, 90, 1.0f / 60);
        require(view.origin == stopped.origin && view.zoom == stopped.zoom, "Suspended camera kept moving");
    });
    test("Repeated minimap targets retain flight momentum and focus remains correct during zoom", [] {
        WorldView view; CameraController camera; camera.stop(view);
        const Vec2 target{38, 27};
        const float initial = distance(view.project(target), screenCenter);
        for (int i = 0; i < 30; ++i) {
            camera.focus(view, target, screenCenter);
            camera.update(view, screenCenter, {}, 1.0f / 60);
        }
        require(distance(view.project(target), screenCenter) < initial * .01f, "Dragging minimap repeatedly reset flight acceleration");
        camera.zoomTo(view, screenCenter, .6f);
        camera.focus(view, target, screenCenter); advance(camera, view, 90, 1.0f / 60);
        require(distance(view.project(target), screenCenter) < .01f, "Concurrent zoom changed focus destination");
    });
}
}
