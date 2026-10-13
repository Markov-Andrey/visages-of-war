#include "GameApplication.hpp"
#include <chrono>
#include <future>

namespace rts::game {
void GameApplication::startBattle() {
    if (menu_.page == rts::MenuPage::Loading) return;
    menu_.player.commanderId = definitions_.commanders().at(menu_.commanderIndex).id;
    menu_.page = rts::MenuPage::Loading;
    menu_.canResume = false;
    menu_.commanderDropdown = menu_.colorDropdown = false;
    dragging_ = panning_ = minimapDragging_ = false;
    if (GetCapture() == window_) ReleaseCapture();
    camera_.stop(view_);
    ui_ = {};
    renderer_.beginLoadingPreview(game_);

    const auto present = [&](const std::wstring& stage, size_t done, size_t total) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { PostQuitMessage(static_cast<int>(message.wParam)); return false; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!IsWindow(window_) || callbackError_) return false;
        refreshCursor();
        renderer_.drawLoading(game_, stage, done, total);
        return true;
    };
    if (!present(L"Подготовка карты", 0, 0)) return;
    // Only CPU data is built off-thread. Direct2D and its caches stay on the UI thread.
    auto pending = std::async(std::launch::async, [this] { return makeMatch(); });
    while (pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        if (!present(L"Подготовка карты", 0, 0)) return;
        MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    game_ = pending.get();
    renderer_.beginLoadingPreview(game_);
    if (!renderer_.prepareMatchAssets(game_, definitions_, present)) return;
    paused_ = false;
    ui_.selection.army(game_);
    resetCamera();
    resetFrameClock_ = true;
    menu_.canResume = true;
    menu_.page = rts::MenuPage::Playing;
}
}
