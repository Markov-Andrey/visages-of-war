#include "GameApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace rts::game {
GameApplication::GameApplication(rts::Paths paths, std::filesystem::path mapFile, bool forgeTest) : paths_(std::move(paths)),
    definitions_(rts::platform::loadDefinitions(paths_)), worldAssets_(rts::WorldAssets::load(paths_)),
    mapFile_(mapFile.empty() ? paths_.asset(L"maps/demo.rtsmap") : std::filesystem::absolute(mapFile)), customMap_(!mapFile.empty()), forgeTest_(forgeTest),
    game_(makeMatch()), renderer_(paths_), cursor_(paths_) {
    menu_.player.commanderId = definitions_.commanders().front().id;
}

int GameApplication::run(HINSTANCE instance, bool smoke, bool smokeMap) {
    verify();
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = instance;
    wc.lpfnWndProc = windowProcedure;
    wc.lpszClassName = L"RTS.MainWindow";
    wc.hCursor = nullptr; // WM_SETCURSOR selects the cursor for the window's current DPI.
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!wc.hIcon || !wc.hIconSm) throw std::runtime_error("Missing project icon resource");
    if (!RegisterClassExW(&wc)) throw std::runtime_error("Window registration failed");
    POINT launchPoint{};
    GetCursorPos(&launchPoint);
    const auto monitor = MonitorFromPoint(launchPoint, MONITOR_DEFAULTTOPRIMARY);
    const auto bounds = monitorBounds(monitor);
    window_ = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Visages of War", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, this);
    if (!window_) throw std::runtime_error("Window creation failed");
    fitToMonitor(monitor);
    if (customMap_) { menu_.page=rts::MenuPage::Playing; menu_.canResume=true; ui_.selection.army(game_); }
    resetCamera();
    ShowWindow(window_, smoke ? SW_HIDE : SW_SHOW);
    if (smokeMap) {
        onMessage(WM_KEYDOWN,VK_F1,0);
        for(int i=0;i<10;++i) game_.tick();
        renderer_.snapshot(game_,rts::Paths::executable().parent_path()/L"forge-play-preview.png");
        if(forgeTest_) PostMessageW(window_,WM_SYSKEYDOWN,VK_F10,0);
    } else if (smoke) exerciseInterface();
    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();
    const auto began = previous;
    double accumulator = 0.0;
    MSG message{};
    bool running = true;
    while (running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) break;
        const auto now = Clock::now();
        const float elapsed = std::min(0.25f, std::chrono::duration<float>(now - previous).count());
        previous = now;
        if (!IsIconic(window_)) {
            if (menu_.page == rts::MenuPage::Playing) moveCamera(elapsed);
            else camera_.stop(view_);
            refreshCursor();
            if (!paused_ && menu_.page == rts::MenuPage::Playing) {
                accumulator += elapsed;
                while (accumulator >= 1.0 / rts::Simulation::ticksPerSecond) {
                    game_.tick();
                    // Future gameplay systems consume events here, after a simulation tick.
                    game_.takeEvents();
                    accumulator -= 1.0 / rts::Simulation::ticksPerSecond;
                }
            }
            if (menu_.page == rts::MenuPage::Playing) {
                const auto hover = !ui_.placement.empty() && mouseInWorld() ? game_.map().pick(mouse_, view_) : std::nullopt;
                ui_.mouse = mouse_;
                ui_.selection.prune(game_);
                renderer_.draw(game_, view_, hover, ui_, grid_, paused_);
            } else {
                accumulator = 0;
                advanceMenu(elapsed);
                renderer_.drawMenu(game_, menu_, definitions_, mouse_);
            }
        } else { accumulator = 0; camera_.stop(view_); }
        if (smoke && now - began > std::chrono::seconds(2)) DestroyWindow(window_);
        MsgWaitForMultipleObjectsEx(0, nullptr, 8, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    if (callbackError_) std::rethrow_exception(callbackError_);
    if (smokeMap && forgeTest_ && !testExitRequested_) throw std::runtime_error("Game smoke: F10 did not exit test play");
    return 0;
}

void GameApplication::verify() {
    renderer_.verifyAssets(); cursor_.verify();
    if (!FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), RT_GROUP_ICON)) throw std::runtime_error("Missing executable icon");
}

void GameApplication::snapshot(const std::filesystem::path& output, bool menu, bool grid, bool title) {
    renderer_.validateCombatAssets(definitions_);
    renderer_.snapshot(game_, output, menu || title ? &definitions_ : nullptr, grid, nullptr, title ? rts::MenuPage::Main : rts::MenuPage::BattleSetup);
}

rts::Simulation GameApplication::makeMatch() const {
    const auto& commander=definitions_.commanders().at(menu_.commanderIndex);
    const auto& depot=definitions_.startingDepot(commander.factionId);
    auto scenario=rts::loadScenario(mapFile_,worldAssets_,{depot.width,depot.height});
    const auto hero=scenario.heroSpawn?commander.startingHero:std::string{};
    return rts::Simulation(std::move(scenario),menu_.player,definitions_.entity(commander.startingWorker),definitions_.entities(),hero,definitions_.progression());
}

RECT GameApplication::monitorBounds(HMONITOR monitor) {
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) throw std::runtime_error("Cannot read monitor bounds");
    return info.rcMonitor;
}

void GameApplication::fitToMonitor(HMONITOR monitor) {
    const auto bounds = monitorBounds(monitor);
    RECT current{};
    GetWindowRect(window_, &current);
    if (!EqualRect(&bounds, &current) && !SetWindowPos(window_, nullptr,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        SWP_NOZORDER | SWP_NOACTIVATE)) throw std::runtime_error("Cannot fit fullscreen window to monitor");
    RECT client{};
    GetClientRect(window_, &client);
    renderer_.resize(client.right, client.bottom);
}

LRESULT CALLBACK GameApplication::windowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    auto* app = reinterpret_cast<GameApplication*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<GameApplication*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window_ = hwnd;
        app->renderer_.attach(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(hwnd, message, wParam, lParam);
    try { return app->onMessage(message, wParam, lParam); }
    catch (...) {
        app->callbackError_ = std::current_exception();
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    }
}
}
