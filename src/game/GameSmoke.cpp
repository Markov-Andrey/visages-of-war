#include "GameApplication.hpp"
#include "rts/Library.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <stdexcept>

namespace rts::game {
void GameApplication::exerciseInterface() {
    const auto checkFullscreen = [&] {
        const auto bounds = monitorBounds(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST));
        RECT windowBounds{}, client{};
        GetWindowRect(window_, &windowBounds); GetClientRect(window_, &client);
        POINT origin{};
        ClientToScreen(window_, &origin);
        const auto style = GetWindowLongPtrW(window_, GWL_STYLE);
        if (!EqualRect(&bounds, &windowBounds) || origin.x != bounds.left || origin.y != bounds.top ||
            client.right != bounds.right - bounds.left || client.bottom != bounds.bottom - bounds.top ||
            (style & (WS_CAPTION | WS_THICKFRAME)) || !(style & WS_POPUP))
            throw std::runtime_error("Smoke: fullscreen client does not cover the monitor");
    };
    checkFullscreen();
    onMessage(WM_SETCURSOR, reinterpret_cast<WPARAM>(window_), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    if (GetCursor() != cursor_.handle(GetDpiForWindow(window_))) throw std::runtime_error("Smoke: custom cursor not applied");
    mouse_ = {rts::MenuLayout(renderer_.size()).battles.x + 20, rts::MenuLayout(renderer_.size()).battles.y + 20};
    if (cursorKind() != rts::CursorKind::Hand) throw std::runtime_error("Smoke: menu button cursor failed");
    mouse_ = {-1, -1};
    onMessage(WM_DISPLAYCHANGE, 32, 0);
    checkFullscreen();
    // Exercise the same Win32 handlers used by mouse and keyboard, in a hidden window.
    {
        const rts::MenuLayout layout(renderer_.size());
        mouse_ = {layout.battles.x + 20, layout.battles.y + 20};
        menuClick();
        const rts::ColorSelectLayout colors(layout.color, renderer_.size().y);
        for (size_t i = 0; i < rts::teamPalettes.size(); ++i) {
            mouse_ = {layout.color.x + 20, layout.color.y + 20};
            if (cursorKind() != rts::CursorKind::Hand) throw std::runtime_error("Smoke: color select cursor failed");
            menuClick();
            if (!menu_.colorDropdown) throw std::runtime_error("Smoke: color select did not open");
            const auto option = colors.option(i);
            mouse_ = {option.x + option.width * .5f, option.y + option.height * .5f};
            menuClick();
            if (menu_.colorDropdown || static_cast<size_t>(menu_.player.color) != i)
                throw std::runtime_error("Smoke: palette mouse selection failed");
        }
        mouse_ = {layout.color.x + 20, layout.color.y + 20};
        menuClick();
        const auto black = colors.option(static_cast<size_t>(rts::TeamColor::Black));
        mouse_ = {black.x + 10, black.y + 10};
        menuClick();
        mouse_ = {layout.color.x + 20, layout.color.y + 20};
        menuClick();
        renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"colors-setup-preview.png", &definitions_, false, nullptr, rts::MenuPage::BattleSetup, 0, &menu_);
        onMessage(WM_KEYDOWN, VK_ESCAPE, 0);
        if (menu_.colorDropdown || menu_.page != rts::MenuPage::BattleSetup)
            throw std::runtime_error("Smoke: palette Escape left setup");
        mouse_ = {layout.start.x + 20, layout.start.y + 20};
        menuClick();
        if (menu_.page != rts::MenuPage::Playing || game_.player().color != rts::TeamColor::Black)
            throw std::runtime_error("Smoke: battle lost selected palette color");
        if (game_.clock().elapsedTicks() != 0) throw std::runtime_error("Smoke: loading advanced simulation time");
        renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"loading-preview.png", &definitions_, false,
            nullptr, rts::MenuPage::Loading);
        rts::MenuState loadingPreview;
        loadingPreview.page = rts::MenuPage::Loading;
        loadingPreview.loadingProgress = 0;
        renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"loading-empty.png", &definitions_, false,
            nullptr, rts::MenuPage::Loading, 0, &loadingPreview, nullptr, {800, 600});
        loadingPreview.loadingProgress = 1;
        renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"loading-complete.png", &definitions_, false,
            nullptr, rts::MenuPage::Loading, 0, &loadingPreview, nullptr, {1920, 1080});
        if (game_.clock().elapsedTicks() != 0 || game_.fog().explored({game_.map().width()-1, game_.map().height()-1}))
            throw std::runtime_error("Smoke: loading preview changed match time or fog");
    }
    {
        const auto ticks = game_.clock().elapsedTicks();
        const auto balance = game_.storedCrystals();
        const auto selection = ui_.selection.ids;
        onMessage(WM_KEYDOWN, VK_ESCAPE, 0);
        const rts::MenuLayout menuLayout(renderer_.size(), true);
        mouse_ = {menuLayout.library.x + 20, menuLayout.library.y + 20};
        if (cursorKind() != rts::CursorKind::Hand) throw std::runtime_error("Smoke: library menu cursor failed");
        menuClick();
        if (menu_.page != rts::MenuPage::Library) throw std::runtime_error("Smoke: library did not open");
        renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"library-preview.png", &definitions_, false, nullptr, rts::MenuPage::Library);
        advanceMenu(.4f);
        renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"library-next-preview.png", &definitions_, false, nullptr, rts::MenuPage::Library, menu_.librarySeconds);
        if (menu_.librarySeconds < .39 || game_.clock().elapsedTicks() != ticks || game_.storedCrystals() != balance || ui_.selection.ids != selection)
            throw std::runtime_error("Smoke: library preview advanced match state");
        const rts::LibraryLayout book(renderer_.size());
        for (size_t i = 0; i < rts::libraryTabs.size(); ++i) {
            menu_.libraryEntry = menu_.libraryEntryScroll = 999;
            menu_.libraryDetailScroll = 999;
            const auto tab = book.tab(i);
            mouse_ = book.origin + rts::Vec2{tab.x + 10, tab.y + 10} * book.scale;
            if (cursorKind() != rts::CursorKind::Hand) throw std::runtime_error("Smoke: library tab cursor failed");
            menuClick();
            if (menu_.libraryTab != rts::libraryTabs[i] || menu_.libraryEntry || menu_.libraryEntryScroll || menu_.librarySeconds != 0 || menu_.libraryDetailScroll != 0)
                throw std::runtime_error("Smoke: library tab retained stale selection");
            const auto factions = rts::libraryFactions(definitions_);
            const auto entries = rts::libraryEntries(definitions_, factions.at(menu_.libraryFaction)->id, menu_.libraryTab);
            if (entries.empty()) throw std::runtime_error("Smoke: catalog category unexpectedly empty");
            const auto file = L"library-tab-" + std::to_wstring(i) + L"-preview.png";
            renderer_.snapshot(game_, rts::Paths::executable().parent_path() / file, &definitions_, false, nullptr,
                rts::MenuPage::Library, 0, &menu_);
            const float height = renderer_.libraryDetailsHeight(definitions_, *entries.front());
            auto longEntry = *entries.front();
            for (int repeat = 0; repeat < 30; ++repeat) longEntry.description += entries.front()->description + " ";
            if (renderer_.libraryDetailsHeight(definitions_, longEntry) <= height + book.details.height)
                throw std::runtime_error("Smoke: long library descriptions were truncated during measurement");
            mouse_ = book.origin + rts::Vec2{book.details.x + 10, book.details.y + 10} * book.scale;
            for (int scroll = 0; scroll < 100; ++scroll) libraryScroll(1);
            const float maximum = std::max(0.0f, height - book.details.height);
            if (menu_.libraryDetailScroll != maximum || menu_.libraryEntryScroll != 0)
                throw std::runtime_error("Smoke: library detail scroll cannot reach the last row or scrolls the entry list");
            renderer_.snapshot(game_, rts::Paths::executable().parent_path() / (L"library-bottom-" + std::to_wstring(i) + L".png"),
                &definitions_, false, nullptr, rts::MenuPage::Library, 0, &menu_, nullptr, {960, 600});
            if (maximum > 0) {
                mouse_ = book.origin + rts::Vec2{book.detailsUp.x + 10, book.detailsUp.y + 10} * book.scale;
                if (cursorKind() != rts::CursorKind::Hand) throw std::runtime_error("Smoke: library scroll button is not interactive");
                menuClick();
                if (menu_.libraryDetailScroll >= maximum) throw std::runtime_error("Smoke: library scroll button did not scroll");
            }
            mouse_ = book.origin + rts::Vec2{book.details.x + 10, book.details.y + 10} * book.scale;
            for (int scroll = 0; scroll < 100; ++scroll) libraryScroll(-1);
            if (menu_.libraryDetailScroll != 0) throw std::runtime_error("Smoke: library cannot scroll back to title");
        }
        mouse_ = book.origin + rts::Vec2{book.tab(0).x + 10, book.tab(0).y + 10} * book.scale;
        menuClick();
        const auto row = book.entryRow(0);
        mouse_ = book.origin + rts::Vec2{row.x + 10, row.y + 10} * book.scale;
        if (cursorKind() != rts::CursorKind::Hand) throw std::runtime_error("Smoke: library entry cursor failed");
        menu_.libraryDetailScroll = 200;
        menuClick();
        if (menu_.librarySeconds != 0 || menu_.libraryDetailScroll != 0) throw std::runtime_error("Smoke: library entry did not select");
        const auto matchColor = game_.player().color;
        const auto openColors = [&] {
            mouse_ = book.origin + rts::Vec2{book.color.x + 10, book.color.y + 10} * book.scale;
            menuClick();
            if (!menu_.colorDropdown) throw std::runtime_error("Smoke: library palette did not open");
        };
        openColors();
        onMessage(WM_KEYDOWN, VK_HOME, 0);
        onMessage(WM_KEYDOWN, VK_ESCAPE, 0);
        if (menu_.colorDropdown || menu_.page != rts::MenuPage::Library || menu_.player.color != matchColor)
            throw std::runtime_error("Smoke: palette cancellation changed color or page");
        openColors();
        onMessage(WM_KEYDOWN, VK_HOME, 0);
        for (int i = 0; i < 5; ++i) onMessage(WM_KEYDOWN, VK_RIGHT, 0);
        onMessage(WM_KEYDOWN, VK_UP, 0); // Fifth row has no cell in this column.
        if (menu_.colorFocus != 23) throw std::runtime_error("Smoke: palette vertical wrap lost its column");
        onMessage(WM_KEYDOWN, VK_DOWN, 0);
        if (menu_.colorFocus != 5) throw std::runtime_error("Smoke: palette focused an empty swatch");
        onMessage(WM_KEYDOWN, VK_END, 0);
        onMessage(WM_KEYDOWN, VK_RIGHT, 0); // Wrap from the last swatch to the first.
        onMessage(WM_KEYDOWN, VK_DOWN, 0);
        onMessage(WM_KEYDOWN, VK_RETURN, 0);
        if (menu_.colorDropdown || menu_.player.color != rts::TeamColor::Gold || game_.player().color != matchColor ||
            game_.clock().elapsedTicks() != ticks || game_.storedCrystals() != balance || ui_.selection.ids != selection)
            throw std::runtime_error("Smoke: library palette changed live match or ignored keyboard");
        openColors();
        renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"colors-library-preview.png", &definitions_, false, nullptr, rts::MenuPage::Library, 0, &menu_);
        mouse_ = book.origin + rts::Vec2{book.back.x + 10, book.back.y + 10} * book.scale;
        menuClick();
        if (menu_.colorDropdown || menu_.page != rts::MenuPage::Library)
            throw std::runtime_error("Smoke: palette dismissal clicked through to Back");
        for (const auto color : {rts::TeamColor::White, rts::TeamColor::Black, rts::TeamColor::Yellow, rts::TeamColor::Gold, rts::TeamColor::Orange}) {
            menu_.player.color = color;
            const auto file = L"library-color-" + std::to_wstring(static_cast<int>(color)) + L"-preview.png";
            renderer_.snapshot(game_, rts::Paths::executable().parent_path() / file, &definitions_, false, nullptr, rts::MenuPage::Library, 0, &menu_);
        }
        mouse_ = book.origin + rts::Vec2{book.back.x + 10, book.back.y + 10} * book.scale;
        menuClick();
        if (menu_.page != rts::MenuPage::Main) throw std::runtime_error("Smoke: library back failed");
        menu_.player.color = matchColor;
        onMessage(WM_KEYDOWN, VK_ESCAPE, 0);
        if (menu_.page != rts::MenuPage::Playing || ui_.selection.ids != selection) throw std::runtime_error("Smoke: library lost resumed match");
        mouse_ = {-1, -1};
    }
    const auto cursorExtent = renderer_.size();
    using rts::CursorKind;
    const std::array<std::pair<rts::Vec2, CursorKind>, 8> edgeCursors{{
        {{cursorExtent.x - 1, cursorExtent.y * .5f}, CursorKind::ScrollEast},
        {{cursorExtent.x - 1, cursorExtent.y - 1}, CursorKind::ScrollSouthEast},
        {{cursorExtent.x * .5f, cursorExtent.y - 1}, CursorKind::ScrollSouth},
        {{0, cursorExtent.y - 1}, CursorKind::ScrollSouthWest},
        {{0, cursorExtent.y * .5f}, CursorKind::ScrollWest},
        {{0, 0}, CursorKind::ScrollNorthWest},
        {{cursorExtent.x * .5f, 0}, CursorKind::ScrollNorth},
        {{cursorExtent.x - 1, 0}, CursorKind::ScrollNorthEast}
    }};
    for (const auto& [point, expected] : edgeCursors) {
        mouse_ = point;
        if (cursorKind() != expected) throw std::runtime_error("Smoke: edge cursor points in the wrong direction");
    }
    dragging_ = true;
    if (cameraEdgeDirection() != rts::Vec2{}) throw std::runtime_error("Smoke: selection drag retained edge cursor");
    dragging_ = false; minimapDragging_ = true;
    if (cameraEdgeDirection() != rts::Vec2{}) throw std::runtime_error("Smoke: minimap drag retained edge cursor");
    minimapDragging_ = false; panning_ = true;
    if (cursorKind() != CursorKind::Move) throw std::runtime_error("Smoke: edge cursor replaced middle drag cursor");
    panning_ = false;
    ui_.selection.ids = {game_.worker().id};
    ui_.placement = "church.barracks";
    // The catalog footprint may grow; find a free site near the original test location.
    std::optional<rts::Cell> constructionSite;
    for (int radius = 0; radius <= 6 && !constructionSite; ++radius)
        for (int y = 17 - radius; y <= 17 + radius && !constructionSite; ++y)
            for (int x = 17 - radius; x <= 17 + radius && !constructionSite; ++x) {
                const rts::Cell cell{x, y};
                if (!game_.canPlace(ui_.placement, cell)) continue;
                mouse_ = view_.project(rts::center(cell), float(game_.map().at(cell).height));
                if (mouseInWorld() && cursorKind() == rts::CursorKind::Build) constructionSite = cell;
            }
    if (!constructionSite) throw std::runtime_error("Smoke: no visible construction site");
    if (cursorKind() != rts::CursorKind::Build) throw std::runtime_error("Smoke: placement cursor failed");
    mouse_ = view_.project(rts::center(game_.hall()), 0);
    if (cursorKind() != rts::CursorKind::Blocked) throw std::runtime_error("Smoke: blocked cursor failed");
    ui_.placement.clear();
    bool gatherCursor = false;
    for (const auto& crystal : game_.crystals()) if (game_.crystalVisible(crystal)) {
        mouse_ = view_.project(crystal.center(), float(game_.map().at(crystal.cell).height)) + rts::Vec2{0, -12};
        if (mouseInWorld() && cursorKind() == rts::CursorKind::Gather) { gatherCursor = true; break; }
    }
    if (!gatherCursor) throw std::runtime_error("Smoke: gather cursor failed");
    mouse_ = {-1, -1};
    const size_t initialUnitCount = game_.units().size();
    const auto at = [&](rts::Vec2 p) {
        const float scale = GetDpiForWindow(window_) / 96.0f;
        return MAKELPARAM(static_cast<short>(p.x * scale), static_cast<short>(p.y * scale));
    };
    const auto click = [&](rts::Vec2 p) {
        onMessage(WM_LBUTTONDOWN, 0, at(p)); onMessage(WM_LBUTTONUP, 0, at(p));
    };
    bool selectedResource = false;
    for (size_t i = 0; i < game_.crystals().size(); ++i) {
        const auto& node = game_.crystals()[i];
        const auto bounds = rts::crystalBounds(game_, node, view_);
        const rts::Vec2 p{bounds.x + bounds.width * .5f, bounds.y + bounds.height * .4f};
        mouse_ = p;
        if (!mouseInWorld() || rts::pickEntity(game_, view_, p) != node.id) continue;
        click(p);
        if (!selectedCrystal() || selectedCrystal()->id != node.id || selectedCrystal()->remaining != node.capacity || cursorKind() != rts::CursorKind::Select)
            throw std::runtime_error("Smoke: neutral crystal selection failed");
        const auto message = game_.message();
        onMessage(WM_RBUTTONDOWN, 0, at(p)); action(1);
        if (game_.message() != message) throw std::runtime_error("Smoke: neutral crystal accepted a player command");
        renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"crystal-preview.png", nullptr, false, &ui_);
        ui_.selection.ids = {game_.worker().id};
        onMessage(WM_RBUTTONDOWN, 0, at(p));
        if (game_.worker().gatherOriginCrystal != static_cast<int>(i) || cursorKind() != rts::CursorKind::Gather)
            throw std::runtime_error("Smoke: resource sprite gather order failed");
        game_.stop(ui_.selection.ids);
        selectedResource = true;
        break;
    }
    if (!selectedResource) throw std::runtime_error("Smoke: no selectable crystal in view");
    const rts::BattleLayout shortcuts(renderer_.size());
    const auto heroId = game_.hero()->id;
    onMessage(WM_KEYDOWN, VK_F1, 0);
    if (ui_.selection.ids.size() != 18 || !ui_.selection.contains(heroId)) throw std::runtime_error("Smoke: F1 army filter failed");
    onMessage(WM_KEYDOWN, VK_F2, 0);
    if (ui_.selection.ids != std::vector<rts::EntityId>{heroId}) throw std::runtime_error("Smoke: F2 hero failed");
    click({shortcuts.army.x + 20, shortcuts.army.y + 20});
    if (ui_.selection.ids.size() != 18) throw std::runtime_error("Smoke: army button failed");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"flying-preview.png", nullptr, false, &ui_);
    click({shortcuts.hero.x + 20, shortcuts.hero.y + 20});
    if (ui_.selection.ids != std::vector<rts::EntityId>{heroId}) throw std::runtime_error("Smoke: hero button failed");
    game_.grantExperience(heroId, 200);
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"hero-preview.png");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"hero-hud-compact.png",
        nullptr, false, &ui_, rts::MenuPage::BattleSetup, 0, nullptr, nullptr, {800, 600});
    rts::GameplayUi tooltipUi;
    tooltipUi.selection.ids = {game_.worker().id};
    const auto command = rts::BattleLayout({1440, 900}).commands[1];
    tooltipUi.mouse = {command.x + 20, command.y + 20};
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"tooltip-preview.png", nullptr, false, &tooltipUi);
    resetCamera();
    if (!grid_) throw std::runtime_error("Smoke: grid must be enabled by default");
    onMessage(WM_KEYDOWN, 'K', 0);
    if (grid_) throw std::runtime_error("Smoke: K did not hide grid");
    onMessage(WM_KEYDOWN, 'K', 0);
    if (!grid_) throw std::runtime_error("Smoke: K did not show grid");
    auto bounds = rts::buildingBounds(game_, game_.buildings().front(), view_);
    click({bounds.x + bounds.width * .5f, bounds.y + bounds.height * .45f});
    if (!selectedStructureOrProducer()) throw std::runtime_error("Smoke: building selection failed");
    const rts::Cell rally{15, 22};
    focus(rts::center(rally), true);
    onMessage(WM_RBUTTONDOWN, 0, at(view_.project(rts::center(rally), 0)));
    if (game_.buildings().front().rally != rally) throw std::runtime_error("Smoke: rally command failed");
    resetCamera();
    const rts::BattleLayout layout(renderer_.size());
    onMessage(WM_KEYDOWN, 'Y', 0);
    click({layout.commands[0].x + 20, layout.commands[0].y + 20});
    clearCommandMode();
    if (game_.buildings().front().production.size() != 1) throw std::runtime_error("Smoke: production button failed");
    rts::Vec2 a{10000, 10000}, b{-10000, -10000};
    for (const auto& u : game_.units()) if (u.owner == game_.player().id && u.definition.canBuild) {
        const auto p = rts::unitScreenAnchor(view_, u.position, game_.unitHeight(u));
        a.x = std::min(a.x, p.x - 28); a.y = std::min(a.y, p.y - 55);
        b.x = std::max(b.x, p.x + 28); b.y = std::max(b.y, p.y + 20);
    }
    onMessage(WM_LBUTTONDOWN, 0, at(a)); onMessage(WM_MOUSEMOVE, MK_LBUTTON, at(b)); onMessage(WM_LBUTTONUP, 0, at(b));
    if (ui_.selection.ids.size() != 6) throw std::runtime_error("Smoke: drag selection failed");
    onMessage(WM_KEYDOWN, 'B', 0);
    onMessage(WM_KEYDOWN, 'B', 0);
    click(view_.project(rts::center(*constructionSite), float(game_.map().at(*constructionSite).height)));
    if (game_.buildings().size() != 2) throw std::runtime_error("Smoke: construction placement failed");
    for (int tick = 0; tick < 700; ++tick) game_.tick();
    if (!game_.buildings().back().complete() || game_.units().size() != initialUnitCount + 1) throw std::runtime_error("Smoke: construction or training failed");
    bounds = rts::buildingBounds(game_, game_.buildings().back(), view_);
    // Builders can now be selected through the roof. Click an exposed part of
    // the barracks instead of assuming that a fixed roof pixel selects it.
    std::optional<rts::Vec2> barracksPoint;
    for (int y = 1; y < 10 && !barracksPoint; ++y) for (int x = 1; x < 10 && !barracksPoint; ++x) {
        const rts::Vec2 p{std::round(bounds.x + bounds.width * x / 10), std::round(bounds.y + bounds.height * y / 10)};
        if (layout.world.contains(p) && rts::pickEntity(game_, view_, p) == game_.buildings().back().id) barracksPoint = p;
    }
    if (!barracksPoint) throw std::runtime_error("Smoke: no exposed barracks area for selection");
    click(*barracksPoint);
    if (!selectedStructureOrProducer() || selectedStructureOrProducer()->definition.id != "church.barracks")
        throw std::runtime_error("Smoke: barracks selection failed");
    onMessage(WM_KEYDOWN, 'Y', 0); onMessage(WM_KEYDOWN, 'Q', 0); clearCommandMode();
    const rts::Cell armyRally{21, 26};
    focus(rts::center(armyRally), true);
    onMessage(WM_RBUTTONDOWN, 0, at(view_.project(rts::center(armyRally), 0)));
    for (int tick = 0; tick < 750; ++tick) game_.tick();
    if (game_.units().size() != initialUnitCount + 2 || game_.units().back().cell != armyRally)
        throw std::runtime_error("Smoke: trained soldier did not reach rally");
    mouse_ = {layout.minimap.x + 10, layout.minimap.y + 10};
    if (mouseInWorld()) throw std::runtime_error("Smoke: raised minimap leaked input into the world");
    const rts::MinimapProjection mini(layout.minimap, game_.map());
    const rts::Cell miniRally{24, 27};
    onMessage(WM_RBUTTONDOWN, 0, at(mini.project(rts::center(miniRally))));
    // At this resolution a logical tile is about one pixel tall; compare with
    // the same pixel-centred picking used by the actual input handler.
    const auto picked = layout.minimapCell(mouse_, game_.map());
    if (!picked || !selectedStructureOrProducer() || selectedStructureOrProducer()->rally != *picked)
        throw std::runtime_error("Smoke: minimap rally projection failed");
    const auto beforeMinimap = view_.origin;
    click(mini.project(rts::center({14, 15})));
    if (view_.origin != beforeMinimap) throw std::runtime_error("Smoke: minimap camera jumped immediately");
    const auto focused = layout.minimapCell(mouse_, game_.map());
    const rts::Vec2 screenCenter{layout.world.width * .5f, layout.world.y + layout.world.height * .5f};
    for (int frame = 0; frame < 90; ++frame) camera_.update(view_, screenCenter, {}, 1.0f / 60);
    if (!focused || !layout.world.contains(view_.project(rts::center(*focused))))
        throw std::runtime_error("Smoke: minimap camera projection failed");
    const auto zoomAnchor = view_.unproject(screenCenter);
    const float beforeZoom = view_.zoom, dpiScale = GetDpiForWindow(window_) / 96.0f;
    POINT wheelPoint{LONG(std::lround(screenCenter.x * dpiScale)), LONG(std::lround(screenCenter.y * dpiScale))};
    if (!ClientToScreen(window_, &wheelPoint)) throw std::runtime_error("Smoke: wheel coordinates failed");
    onMessage(WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(wheelPoint.x, wheelPoint.y));
    if (view_.zoom != beforeZoom) throw std::runtime_error("Smoke: mouse wheel zoom jumped immediately");
    for (int frame = 0; frame < 20; ++frame) camera_.update(view_, screenCenter, {}, 1.0f / 60);
    const auto projectedAnchor = view_.project(zoomAnchor);
    if (view_.zoom <= beforeZoom || std::hypot(projectedAnchor.x - screenCenter.x, projectedAnchor.y - screenCenter.y) > .05f)
        throw std::runtime_error("Smoke: smooth wheel zoom lost its cursor anchor");
    onMessage(WM_ACTIVATEAPP, FALSE, 0);
    const auto inactiveView = view_;
    for (int frame = 0; frame < 60; ++frame) camera_.update(view_, screenCenter, {}, 1.0f / 60);
    if (view_.origin != inactiveView.origin || view_.zoom != inactiveView.zoom)
        throw std::runtime_error("Smoke: camera retained motion after focus loss");
    onMessage(WM_MBUTTONDOWN, 0, at(screenCenter));
    for (const auto point : {rts::Vec2{-4000, -4000}, rts::Vec2{4000, 4000}}) {
        onMessage(WM_MOUSEMOVE, MK_MBUTTON, at(point));
        const auto top = view_.origin + game_.map().groundMinimum() * view_.zoom, bottom = top + game_.map().groundExtent() * view_.zoom;
        const float margin = cameraTuning.borderMargin + .02f;
        if ((bottom.x - top.x >= layout.world.width && (top.x > layout.world.x + margin || bottom.x < layout.world.x + layout.world.width - margin)) ||
            (bottom.y - top.y >= layout.world.height && (top.y > layout.world.y + margin || bottom.y < layout.world.y + layout.world.height - margin)))
            throw std::runtime_error("Smoke: middle dragging escaped camera bounds");
    }
    onMessage(WM_MBUTTONUP, 0, at(screenCenter));
    auto arena = rts::loadScenario(paths_.asset(L"maps/demo.rtsmap"));
    arena.units = {{"church.peacemaker", 0, {15, 18}}, {"church.peacemaker", 1, {17, 19}}};
    game_ = rts::Simulation(std::move(arena), {}, definitions_.entity("church.worker"), definitions_.entities(), definitions_.startingDepot(definitions_.entity("church.worker").factionId).id, {}, definitions_.progression(), {}, definitions_.armorRules());
    ui_ = {}; resetCamera();
    onMessage(WM_KEYDOWN, VK_F1, 0);
    if (ui_.selection.ids.size() != 1 || game_.unit(ui_.selection.ids.front())->owner != game_.player().id)
        throw std::runtime_error("Smoke: F1 did not select only friendly soldiers");
    const auto fighter = ui_.selection.ids.front();
    const auto enemy = game_.units().back().id;
    const auto enemyBounds = rts::unitBounds(game_, *game_.unit(enemy), view_);
    onMessage(WM_RBUTTONDOWN, 0, at({enemyBounds.x + enemyBounds.width * .5f, enemyBounds.y + enemyBounds.height * .4f}));
    if (game_.unit(fighter)->attackTarget != enemy) throw std::runtime_error("Smoke: right click on enemy sprite did not attack");
    if (cursorKind() != rts::CursorKind::Attack) throw std::runtime_error("Smoke: attack cursor failed");
    for (int tick = 0; tick < 180 && game_.unit(enemy)->health == game_.unit(enemy)->definition.maximumHealth; ++tick) game_.tick();
    if (game_.unit(enemy)->health == game_.unit(enemy)->definition.maximumHealth) throw std::runtime_error("Smoke: ordered attack caused no damage");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"combat-preview.png");
    const auto combatUnitCount = game_.units().size();
    const auto& fighterType = game_.unit(fighter)->definition;
    const int hitDamage = game_.armorRules().damage(fighterType.attackDamage, game_.unit(enemy)->armor());
    const int hitsToKill = (game_.unit(enemy)->maximumHealth() + hitDamage - 1) / hitDamage;
    const int duelTicks = hitsToKill * (fighterType.attackWindupTicks + fighterType.attackRecoveryTicks + fighterType.attackCooldownTicks + 1) + 60;
    for (int tick = 0; tick < duelTicks && game_.units().size() == combatUnitCount; ++tick) game_.tick();
    ui_.selection.prune(game_);
    if (game_.units().size() == combatUnitCount || game_.corpses().empty()) throw std::runtime_error("Smoke: no unit died in combat");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"death-preview.png");
    auto heroScenario = rts::loadScenario(paths_.asset(L"maps/demo.rtsmap"));
    // This fixture tests the fallen-hero UI. Keep the duel isolated: a nearby
    // worker can be closer in the ground-plane metric and attract the attacker.
    heroScenario.extraWorkers.clear();
    heroScenario.units = {{"church.hero", 0, {15, 18}}, {"church.peacemaker", 1, {16, 18}}};
    auto heroTypes = definitions_.entities();
    for (auto& type : heroTypes) if (type.id == "church.hero") { type.maximumHealth = 1; type.attackDamage = 0; }
    game_ = rts::Simulation(std::move(heroScenario), menu_.player, definitions_.entity("church.worker"), std::move(heroTypes), definitions_.startingDepot(definitions_.entity("church.worker").factionId).id, {}, definitions_.progression(), {}, definitions_.armorRules());
    onMessage(WM_KEYDOWN, VK_F2, 0);
    for (int tick = 0; tick < 30 && game_.hero(); ++tick) game_.tick();
    if (game_.hero() || !game_.heroFallen()) throw std::runtime_error("Smoke: hero did not die");
    ui_.selection.prune(game_);
    ui_.selection.ids = {game_.worker().id};
    onMessage(WM_KEYDOWN, VK_F2, 0);
    click({shortcuts.hero.x + 20, shortcuts.hero.y + 20});
    if (ui_.selection.ids != std::vector<rts::EntityId>{game_.worker().id}) throw std::runtime_error("Smoke: dead hero button changed selection");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"hero-dead-preview.png");
    game_.takeEvents();
    exerciseRangedCombat();
    exerciseUnitCommands();
    exerciseInspection();
    exerciseHarvestFeedback();
    // Exercise the real Enter / WM_CHAR route, including accidental hotkeys and new-match reset.
    startBattle();
    const rts::Cell revealProbe{game_.map().width()-1,game_.map().height()-1};
    const auto selected=ui_.selection.ids; const bool wasGrid=grid_;
    onMessage(WM_KEYDOWN,VK_RETURN,0); onMessage(WM_CHAR,L'\r',0);
    if(!ui_.consoleOpen||cameraInputAllowed()) throw std::runtime_error("Console did not capture input");
    for(wchar_t c:std::wstring(L"unknown")) onMessage(WM_CHAR,c,0);
    onMessage(WM_KEYDOWN,VK_RETURN,0);
    if(!ui_.consoleOpen||ui_.consoleReply.empty()||game_.fog().visible(revealProbe)) throw std::runtime_error("Unknown console command changed fog");
    onMessage(WM_KEYDOWN,VK_ESCAPE,0);
    if(ui_.consoleOpen||menu_.page!=rts::MenuPage::Playing) throw std::runtime_error("Console Escape opened the menu");
    onMessage(WM_KEYDOWN,VK_RETURN,0); onMessage(WM_CHAR,L'\r',0);
    for(wchar_t c:std::wstring(L"iseedeadpeoplex")) { onMessage(WM_KEYDOWN,std::towupper(c),0); onMessage(WM_CHAR,c,0); }
    onMessage(WM_CHAR,L'\b',0); onMessage(WM_KEYDOWN,'K',0);
    if(ui_.consoleInput!=L"iseedeadpeople"||grid_!=wasGrid||ui_.selection.ids!=selected||ui_.orderMode||!ui_.placement.empty())
        throw std::runtime_error("Console typing triggered gameplay hotkeys");
    renderer_.snapshot(game_,rts::Paths::executable().parent_path()/L"console-input-preview.png",nullptr,false,&ui_);
    onMessage(WM_KEYDOWN,VK_RETURN,0); onMessage(WM_CHAR,L'\r',0);
    for(int i=0;i<10;++i) game_.tick();
    if(ui_.consoleOpen||!game_.fog().visible(revealProbe)) throw std::runtime_error("Console reveal did not persist");
    renderer_.snapshot(game_,rts::Paths::executable().parent_path()/L"console-reveal-preview.png",nullptr,false,&ui_);
    startBattle();
    if(game_.fog().explored(revealProbe)) throw std::runtime_error("New match retained console reveal");

}
}
