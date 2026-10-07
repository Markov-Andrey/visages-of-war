#pragma once
#include "rts/Renderer.hpp"
#include "rts/GameCursor.hpp"
#include <exception>
#include <memory>
#include "rts/WorldEditor.hpp"

namespace rts::forge {
class ForgeApplication {
public:
    explicit ForgeApplication(rts::Paths paths,const std::filesystem::path& mapFile={});
    ~ForgeApplication();
    void verify();
    void snapshot(const std::filesystem::path& output, bool overview = false);
    int run(HINSTANCE instance,bool smoke);
private:
    static RECT monitorBounds(HMONITOR monitor);
    void fitToMonitor(HMONITOR monitor);
    void resetCamera();
    rts::Vec2 mousePosition(LPARAM data) const;
    bool mouseInWorld() const;
    rts::CursorKind cursorKind() const;
    void refreshCursor();
    void moveCamera(float dt);
    void launchGame();
    void pollGame();
    void editorError(const std::exception& error);
    std::filesystem::path chooseMap(bool save);
    bool saveEditor(bool saveAs=false);
    bool confirmEditorChanges();
    void testEditorMap();
    void editorAction(size_t action);
    void editorValue(int step);
    void editorClick();
    void editorKey(WPARAM key);
    void exerciseInterface();
    static LRESULT CALLBACK windowProcedure(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) noexcept;
    LRESULT onMessage(UINT message,WPARAM wParam,LPARAM lParam);
    rts::Paths paths_;
    rts::Definitions definitions_;
    rts::WorldAssets worldAssets_;
    std::unique_ptr<rts::WorldEditor> editor_;
    rts::Renderer renderer_;
    rts::GameCursor cursor_;
    HWND window_{};
    rts::WorldView view_;
    rts::Vec2 mouse_{-1,-1},panStart_{};
    bool grid_{true},panning_{},editorPainting_{},smoke_{},testFinished_{};
    HANDLE gameProcess_{};
    std::filesystem::path testFile_;
    size_t paintAtTest_{},decorAtTest_{},unitsAtTest_{};
    bool dirtyAtTest_{};
    std::exception_ptr callbackError_;
};
}
