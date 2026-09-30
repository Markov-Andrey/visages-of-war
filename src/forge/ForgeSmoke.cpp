#include "ForgeApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace rts::forge {
void ForgeApplication::exerciseInterface() {
    wchar_t title[64]{}; GetWindowTextW(window_,title,64);
    if(std::wstring_view(title)!=L"Visages Forge") throw std::runtime_error("Forge smoke: incorrect window identity");
    const auto at=[&](rts::Vec2 p){const float dpi=GetDpiForWindow(window_)/96.0f;return MAKELPARAM(static_cast<short>(p.x*dpi),static_cast<short>(p.y*dpi));};
    const auto click=[&](rts::Vec2 p){onMessage(WM_LBUTTONDOWN,0,at(p));onMessage(WM_LBUTTONUP,0,at(p));};
    const auto uiClick=[&](rts::UiRect r){click({r.x+12,r.y+12});};
    const rts::EditorLayout layout(renderer_.size());
    uiClick(layout.tools[0]); uiClick(layout.choices[1]);
    const auto begin=view_.project({16.25f,18.3f},0),end=view_.project({20.2f,19.3f},0);
    onMessage(WM_LBUTTONDOWN,0,at(begin)); onMessage(WM_MOUSEMOVE,MK_LBUTTON,at(end)); onMessage(WM_LBUTTONUP,0,at(end));
    const auto strokes=editor_->scenario().landscape.paint.size();
    if(strokes<3) throw std::runtime_error("Forge smoke: texture stroke failed");
    uiClick(layout.actions[6]); if(!editor_->scenario().landscape.paint.empty()) throw std::runtime_error("Forge smoke: undo failed");
    uiClick(layout.actions[7]); if(editor_->scenario().landscape.paint.size()!=strokes) throw std::runtime_error("Forge smoke: redo failed");
    uiClick(layout.tools[2]); click(view_.project({18.27f,18.72f},0));
    uiClick(layout.tools[4]); uiClick(layout.choices[1]); click(view_.project({18.5f,17.5f},0));
    if(editor_->scenario().landscape.decorations.size()!=1||editor_->scenario().units.size()!=23) throw std::runtime_error("Forge smoke: object or unit placement failed");
    editor_->file=rts::Paths::executable().parent_path()/L"Forge — проверка сохранения.rtsmap";
    uiClick(layout.actions[3]); if(editor_->dirty) throw std::runtime_error("Forge smoke: save failed");
    const auto loaded=rts::loadScenario(editor_->file,worldAssets_,editor_->scenario().hallFootprint);
    if(loaded.landscape.paint.size()!=strokes||loaded.units.size()!=23) throw std::runtime_error("Forge smoke: save did not round trip");
    uiClick(layout.tools[0]); uiClick(layout.choices[2]); click(view_.project({17.3f,18.3f},0));
    if(!editor_->dirty) throw std::runtime_error("Forge smoke: unsaved test fixture missing");
    renderer_.snapshotEditor(*editor_,rts::Paths::executable().parent_path()/L"forge-editor-preview.png");
    onMessage(WM_KEYDOWN,VK_F9,0);
    if(!gameProcess_) throw std::runtime_error("Forge smoke: F9 did not launch game");
    const auto preview=rts::loadScenario(testFile_,worldAssets_,editor_->scenario().hallFootprint);
    if(preview.landscape.paint!=editor_->scenario().landscape.paint) throw std::runtime_error("Forge smoke: test ignored unsaved edits");
}
}
