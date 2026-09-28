#include "ForgeApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace rts::forge {
ForgeApplication::ForgeApplication(rts::Paths paths,const std::filesystem::path& mapFile) : paths_(std::move(paths)),
    definitions_(rts::platform::loadDefinitions(paths_)),worldAssets_(rts::WorldAssets::load(paths_)),renderer_(paths_),cursor_(paths_) {
    const auto source=mapFile.empty()?paths_.asset(L"maps/demo.rtsmap"):std::filesystem::absolute(mapFile);
    editor_=std::make_unique<rts::WorldEditor>(rts::loadScenario(source,worldAssets_),worldAssets_,definitions_);
    if(!mapFile.empty()) editor_->file=source;
}

ForgeApplication::~ForgeApplication() { if(gameProcess_) CloseHandle(gameProcess_); }

void ForgeApplication::verify() {
    renderer_.verifyAssets(); cursor_.verify();
    if(!FindResourceW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),RT_GROUP_ICON)) throw std::runtime_error("Missing Forge icon");
}

void ForgeApplication::snapshot(const std::filesystem::path& output) { renderer_.snapshotEditor(*editor_,output); }

int ForgeApplication::run(HINSTANCE instance,bool smoke) {
    smoke_=smoke; verify();
    WNDCLASSEXW wc{sizeof(wc)}; wc.hInstance=instance; wc.lpfnWndProc=windowProcedure; wc.lpszClassName=L"VisagesForge.MainWindow";
    wc.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
    wc.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
    if(!RegisterClassExW(&wc)) throw std::runtime_error("Cannot register Forge window");
    POINT pointer{}; GetCursorPos(&pointer); const auto monitor=MonitorFromPoint(pointer,MONITOR_DEFAULTTOPRIMARY);
    const auto bounds=monitorBounds(monitor);
    window_=CreateWindowExW(WS_EX_APPWINDOW,wc.lpszClassName,L"Visages Forge",WS_POPUP|WS_SYSMENU|WS_MINIMIZEBOX,
        bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,this);
    if(!window_) throw std::runtime_error("Cannot create Forge window");
    fitToMonitor(monitor); resetCamera(); ShowWindow(window_,smoke?SW_HIDE:SW_SHOW);
    if(smoke) exerciseInterface();
    using Clock=std::chrono::steady_clock;
    auto previous=Clock::now(); const auto began=previous;
    MSG message{}; bool running=true;
    while(running) {
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT) {running=false;break;}
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if(!running) break;
        const auto now=Clock::now(); const float elapsed=std::min(.25f,std::chrono::duration<float>(now-previous).count()); previous=now;
        pollGame();
        if(!IsIconic(window_)) {
            moveCamera(elapsed); refreshCursor(); renderer_.drawEditor(*editor_,view_,mouse_,grid_);
        }
        if(smoke&&testFinished_) DestroyWindow(window_);
        if(smoke&&now-began>std::chrono::seconds(20)) throw std::runtime_error("Forge smoke test timed out waiting for game");
        MsgWaitForMultipleObjectsEx(0,nullptr,8,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    if(callbackError_) std::rethrow_exception(callbackError_);
    return 0;
}

RECT ForgeApplication::monitorBounds(HMONITOR monitor) {
    MONITORINFO info{sizeof(info)}; if(!GetMonitorInfoW(monitor,&info)) throw std::runtime_error("Cannot read monitor bounds"); return info.rcMonitor;
}

void ForgeApplication::fitToMonitor(HMONITOR monitor) {
    const auto bounds=monitorBounds(monitor); RECT current{}; GetWindowRect(window_,&current);
    if(!EqualRect(&bounds,&current)&&!SetWindowPos(window_,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER|SWP_NOACTIVATE))
        throw std::runtime_error("Cannot fit Forge window");
    RECT client{}; GetClientRect(window_,&client); renderer_.resize(client.right,client.bottom);
}

LRESULT CALLBACK ForgeApplication::windowProcedure(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) noexcept {
    auto* app=reinterpret_cast<ForgeApplication*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        app=static_cast<ForgeApplication*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); app->window_=hwnd; app->renderer_.attach(hwnd);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
    }
    if(!app) return DefWindowProcW(hwnd,message,wParam,lParam);
    try {return app->onMessage(message,wParam,lParam);} catch(...) {app->callbackError_=std::current_exception();PostQuitMessage(1);return 0;}
}
}
