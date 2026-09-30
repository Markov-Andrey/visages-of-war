#pragma once
#include "rts/Renderer.hpp"
#include "rts/GameCursor.hpp"
#include <exception>
#include <memory>

namespace rts::game {
class GameApplication {
public:
    explicit GameApplication(rts::Paths paths, std::filesystem::path mapFile = {}, bool forgeTest = false);
    int run(HINSTANCE instance, bool smoke, bool smokeMap = false);
    void verify();
    void snapshot(const std::filesystem::path& output, bool menu, bool grid, bool title);
private:
    rts::Simulation makeMatch() const;
    static RECT monitorBounds(HMONITOR monitor);
    void fitToMonitor(HMONITOR monitor);
    void startBattle();
    void selectArmy();
    void selectHero();
    void menuClick();
    bool colorSelectClick(const rts::ColorSelectLayout& layout, rts::Vec2 mouse);
    bool colorSelectKey(WPARAM key);
    void libraryClick();
    void libraryScroll(int direction);
    bool libraryLinkAt(rts::Vec2 mouse) const;
    void advanceMenu(float elapsed);
    const rts::Building* selectedBuilding() const;
    const rts::Crystal* selectedCrystal() const;
    void focus(rts::Vec2 world);
    void action(size_t index);
    std::optional<rts::Cell> pickCommandTarget() const;
    void rightClick(std::optional<rts::Cell> target);
    rts::CursorKind cursorKind() const;
    void refreshCursor();
    void exerciseInterface();
    void exerciseRangedCombat();
    static LRESULT CALLBACK windowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    bool mouseInWorld() const;
    void resetCamera();
    rts::Vec2 mousePosition(LPARAM lParam) const;
    void moveCamera(float dt);
    LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam);
    rts::Paths paths_;
    rts::Definitions definitions_;
    rts::WorldAssets worldAssets_;
    std::filesystem::path mapFile_;
    bool customMap_{}, forgeTest_{}, testExitRequested_{};
    rts::MenuState menu_;
    rts::Simulation game_;
    rts::Renderer renderer_;
    rts::GameCursor cursor_;
    HWND window_{};
    rts::WorldView view_;
    rts::Vec2 mouse_{-1, -1};
    rts::GameplayUi ui_;
    rts::Vec2 dragStart_{}, panStart_{};
    bool dragging_{}, adding_{}, panning_{}, minimapDragging_{}, grid_{true}, paused_{};
    std::exception_ptr callbackError_;
};
}
