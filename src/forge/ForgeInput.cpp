#include "ForgeApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace rts::forge {
void ForgeApplication::resetCamera() {
    view_.zoom=.85f; const auto area=rts::EditorLayout(renderer_.size()).world;
    view_.origin=view_.origin+rts::Vec2{area.width*.5f,area.y+area.height*.45f}-view_.project(rts::center(editor_->scenario().hall)+rts::Vec2{1,2},0);
}

rts::Vec2 ForgeApplication::mousePosition(LPARAM data) const {
    const float dpi=96.0f/GetDpiForWindow(window_); return {GET_X_LPARAM(data)*dpi,GET_Y_LPARAM(data)*dpi};
}

bool ForgeApplication::mouseInWorld() const { return rts::EditorLayout(renderer_.size()).world.contains(mouse_); }

rts::CursorKind ForgeApplication::cursorKind() const { return panning_?rts::CursorKind::Move:mouseInWorld()?rts::CursorKind::Default:rts::CursorKind::Hand; }

void ForgeApplication::refreshCursor() {
    POINT p{}; if(GetForegroundWindow()==window_&&GetCursorPos(&p)&&(GetCapture()==window_||WindowFromPoint(p)==window_)) SetCursor(cursor_.handle(GetDpiForWindow(window_),cursorKind()));
}

void ForgeApplication::moveCamera(float dt) {
    if(GetForegroundWindow()!=window_||(GetAsyncKeyState(VK_CONTROL)&0x8000)) return;
    const auto down=[](int key){return (GetAsyncKeyState(key)&0x8000)!=0;}; const float speed=580*dt;
    if(down('A')||down(VK_LEFT)) view_.origin.x+=speed;
    if(down('D')||down(VK_RIGHT)) view_.origin.x-=speed;
    if(down('W')||down(VK_UP)) view_.origin.y+=speed;
    if(down('S')||down(VK_DOWN)) view_.origin.y-=speed;
}

void ForgeApplication::editorValue(int step) {
    using T=rts::EditorTool;
    if(editor_->tool==T::Paint || editor_->tool==T::ErasePaint) editor_->hardness=std::clamp(editor_->hardness+step*.1f,0.0f,1.0f);
    else if(editor_->tool==T::Decoration) editor_->scale=std::clamp(editor_->scale+step*.1f,.1f,8.0f);
    else if(editor_->tool==T::Unit) editor_->owner=1-editor_->owner;
    else if(editor_->tool==T::Ramp) editor_->direction=(editor_->direction+step+4)%4;
    else editor_->height=std::clamp(editor_->height+step,-1,3);
}

void ForgeApplication::editorClick() {
    const rts::EditorLayout layout(renderer_.size());
    for(size_t i=0;i<layout.actions.size();++i) if(layout.actions[i].contains(mouse_)) {editorAction(i);return;}
    for(size_t i=0;i<layout.tools.size();++i) if(layout.tools[i].contains(mouse_)) {editor_->setTool(static_cast<rts::EditorTool>(i));return;}
    const auto list=editor_->choices(); const size_t first=editor_->choice/8*8;
    for(size_t i=0;i<layout.choices.size();++i) if(layout.choices[i].contains(mouse_)&&first+i<list.size()) {
        editor_->choice=first+i;
        if(editor_->tool==rts::EditorTool::Base) { editor_->beginStroke(); editor_->apply(rts::center(editor_->scenario().worker)); editor_->endStroke(); }
        return;
    }
    if(layout.previous.contains(mouse_)&&first>=8) editor_->choice=first-8;
    if(layout.next.contains(mouse_)&&first+8<list.size()) editor_->choice=first+8;
    if(layout.radiusMinus.contains(mouse_)) editor_->radius=std::max(.25f,editor_->radius-.25f);
    if(layout.radiusPlus.contains(mouse_)) editor_->radius=std::min(16.0f,editor_->radius+.25f);
    if(layout.opacityMinus.contains(mouse_)) editor_->opacity=std::max(.05f,editor_->opacity-.05f);
    if(layout.opacityPlus.contains(mouse_)) editor_->opacity=std::min(1.0f,editor_->opacity+.05f);
    if(layout.valueMinus.contains(mouse_)) editorValue(-1);
    if(layout.valuePlus.contains(mouse_)) editorValue(1);
    if(layout.world.contains(mouse_)) if(const auto p=editor_->scenario().map.pickPosition(mouse_,view_)) {
        editor_->beginStroke(); editor_->apply(*p); editorPainting_=true; SetCapture(window_);
    }
}

void ForgeApplication::editorKey(WPARAM key) {
    const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0;
    if(control) {
        if(key=='S') editorAction((GetKeyState(VK_SHIFT)&0x8000)?4:3);
        if(key=='O') editorAction(2);
        if(key=='N') editorAction(1);
        if(key=='Z') editorAction(6);
        if(key=='Y') editorAction(7);
        return;
    }
    if(key==VK_ESCAPE) {editor_->endStroke();ReleaseCapture();}
    if(key==VK_HOME) resetCamera();
    if(key=='G') grid_=!grid_;
    if(key==VK_F9) editorAction(5);
    if(key==VK_DELETE) editor_->setTool(rts::EditorTool::Remove);
    if(key==VK_OEM_4) editor_->radius=std::max(.25f,editor_->radius-.25f);
    if(key==VK_OEM_6) editor_->radius=std::min(16.0f,editor_->radius+.25f);
    if(key=='R') {editor_->direction=(editor_->direction+1)%4;editor_->rotation=std::fmod(editor_->rotation+15,360.0f);}
    if(key==VK_F5) try {
        auto assets=rts::WorldAssets::load(paths_);
        auto definitions=rts::platform::loadDefinitions(paths_);
        auto check=*editor_; check.reloadDefinitions(assets);
        rts::WorldEditor validated(check.scenario(),assets,definitions); validated.validateForPlay();
        renderer_.validateWorldAssets(assets); renderer_.validateCombatAssets(definitions);
        worldAssets_=std::move(assets); definitions_=std::move(definitions);
        editor_->reloadDefinitions(worldAssets_); renderer_.reloadWorldAssets(worldAssets_);
        editor_->message=L"Каталоги и изображения перечитаны. История отмены очищена.";
    } catch(const std::exception& e) {editorError(e);}
}

LRESULT ForgeApplication::onMessage(UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_CLOSE: if(confirmEditorChanges()) DestroyWindow(window_); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SETCURSOR: if(LOWORD(lParam)==HTCLIENT) {SetCursor(cursor_.handle(GetDpiForWindow(window_),cursorKind()));return TRUE;} break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {PAINTSTRUCT paint{};BeginPaint(window_,&paint);EndPaint(window_,&paint);return 0;}
    case WM_SIZE: renderer_.resize(LOWORD(lParam),HIWORD(lParam)); return 0;
    case WM_DPICHANGED: fitToMonitor(MonitorFromRect(reinterpret_cast<RECT*>(lParam),MONITOR_DEFAULTTONEAREST));return 0;
    case WM_DISPLAYCHANGE: if(!IsIconic(window_)) fitToMonitor(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST));return 0;
    case WM_ACTIVATEAPP:
        if(wParam&&!IsIconic(window_)) fitToMonitor(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST));
        if(!wParam&&GetCapture()==window_) ReleaseCapture(); return 0;
    case WM_MOUSEMOVE:
        mouse_=mousePosition(lParam);
        if(panning_) {view_.origin=view_.origin+mouse_-panStart_;panStart_=mouse_;}
        if(editorPainting_&&mouseInWorld()) if(const auto p=editor_->scenario().map.pickPosition(mouse_,view_)) editor_->apply(*p);
        refreshCursor();return 0;
    case WM_CAPTURECHANGED: if(editorPainting_) editor_->endStroke();editorPainting_=false;panning_=false;return 0;
    case WM_MBUTTONDOWN: panning_=true;panStart_=mousePosition(lParam);SetCapture(window_);return 0;
    case WM_MBUTTONUP: panning_=false;ReleaseCapture();return 0;
    case WM_LBUTTONDOWN: mouse_=mousePosition(lParam);editorClick();return 0;
    case WM_LBUTTONUP: mouse_=mousePosition(lParam);editor_->endStroke();editorPainting_=false;ReleaseCapture();return 0;
    case WM_RBUTTONDOWN: editor_->endStroke();ReleaseCapture();return 0;
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(window_,&p);
        const float dpi=96.0f/GetDpiForWindow(window_); mouse_={p.x*dpi,p.y*dpi};
        if(!mouseInWorld()) return 0;
        const float previous=view_.zoom;view_.zoom=std::clamp(previous*std::pow(1.15f,GET_WHEEL_DELTA_WPARAM(wParam)/120.0f),.35f,2.0f);
        view_.origin=mouse_-(mouse_-view_.origin)*(view_.zoom/previous);return 0;
    }
    case WM_KEYDOWN: if(!(lParam&(1LL<<30))) editorKey(wParam);return 0;
    }
    return DefWindowProcW(window_,message,wParam,lParam);
}
}
