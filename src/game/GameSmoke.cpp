#include "GameApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace rts::game {
void GameApplication::exerciseInterface() {
    cursor_.snapshot(rts::Paths::executable().parent_path() / L"cursors-preview.png");
    {
        rts::Scenario forest{rts::Map(28, 28), {8, 11}, {12, 14}, {{{18, 14}, 1000}}};
        for (int y = 11; y < 13; ++y) for (int x = 8; x < 10; ++x) forest.map.occupy({x, y});
        forest.map.occupy({18, 14});
        std::vector<rts::EntityId> clearing;
        for (int y = 5; y <= 23; ++y) for (int x : {15, 16}) {
            const auto id = 2000 + static_cast<rts::EntityId>(forest.environment.size());
            forest.environment.push_back(rts::makeEnvironment("TREE", id, {x, y}));
            forest.map.occupy({x, y});
            if (y == 14 || y == 15) clearing.push_back(id);
        }
        forest.units = {{"human.soldier", 1, {18, 15}}};
        auto forestTypes = definitions_.entities();
        for (auto& type : forestTypes) if (type.acceptsCargo) type.dayVision = type.nightVision = 1;
        auto observer = definitions_.entity("human.worker"); observer.dayVision = observer.nightVision = 7;
        rts::Simulation forestGame(std::move(forest), {}, observer, std::move(forestTypes));
        rts::GameplayUi forestUi; forestUi.selection.ids = {forestGame.worker().id};
        const auto imagePath = rts::Paths::executable().parent_path();
        if (!forestGame.fog().visible({15, 14}) || forestGame.fog().visible({16, 14}) || forestGame.fog().visible({18, 15}))
            throw std::runtime_error("Smoke: forest sight boundary failed");
        renderer_.snapshot(forestGame, imagePath / L"forest-preview.png", nullptr, false, &forestUi);
        const auto count = forestGame.environment().size();
        for (auto id : clearing) forestGame.setEnvironmentHealth(id, 0);
        if (!forestGame.fog().visible({18, 15})) throw std::runtime_error("Smoke: cleared forest did not reveal enemy");
        renderer_.snapshot(forestGame, imagePath / L"forest-cleared-preview.png", nullptr, false, &forestUi);
        for (auto id : clearing) forestGame.setEnvironmentHealth(id, 100);
        if (forestGame.environment().size() != count || forestGame.fog().visible({18, 15}))
            throw std::runtime_error("Smoke: restored forest did not hide enemy");
        renderer_.snapshot(forestGame, imagePath / L"forest-restored-preview.png", nullptr, false, &forestUi);
    }
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
    startBattle();
    ui_.selection.ids = {game_.worker().id};
    ui_.placement = "human.barracks";
    mouse_ = view_.project(rts::center({17, 17}), 0);
    if (cursorKind() != rts::CursorKind::Build) throw std::runtime_error("Smoke: placement cursor failed");
    mouse_ = view_.project(rts::center(game_.hall()), 0);
    if (cursorKind() != rts::CursorKind::Blocked) throw std::runtime_error("Smoke: blocked cursor failed");
    ui_.placement.clear();
    bool gatherCursor = false;
    for (const auto& crystal : game_.crystals()) if (game_.fog().visible(crystal.cell)) {
        mouse_ = view_.project(rts::center(crystal.cell), float(game_.map().at(crystal.cell).height)) + rts::Vec2{0, -12};
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
        if (!selectedCrystal() || selectedCrystal()->id != node.id || selectedCrystal()->remaining != rts::Crystal::maximum || cursorKind() != rts::CursorKind::Select)
            throw std::runtime_error("Smoke: neutral crystal selection failed");
        const auto message = game_.message();
        onMessage(WM_RBUTTONDOWN, 0, at(p)); action(3);
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
    if (ui_.selection.ids.size() != 17 || !ui_.selection.contains(heroId)) throw std::runtime_error("Smoke: F1 army filter failed");
    onMessage(WM_KEYDOWN, VK_F2, 0);
    if (ui_.selection.ids != std::vector<rts::EntityId>{heroId}) throw std::runtime_error("Smoke: F2 hero failed");
    click({shortcuts.army.x + 20, shortcuts.army.y + 20});
    if (ui_.selection.ids.size() != 17) throw std::runtime_error("Smoke: army button failed");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"flying-preview.png", nullptr, false, &ui_);
    click({shortcuts.hero.x + 20, shortcuts.hero.y + 20});
    if (ui_.selection.ids != std::vector<rts::EntityId>{heroId}) throw std::runtime_error("Smoke: hero button failed");
    game_.grantExperience(heroId, 200);
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"hero-preview.png");
    rts::GameplayUi tooltipUi;
    tooltipUi.selection.ids = {game_.worker().id};
    const auto command = rts::BattleLayout({1440, 900}).commands[1];
    tooltipUi.mouse = {command.x + 20, command.y + 20};
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"tooltip-preview.png", nullptr, false, &tooltipUi);
    resetCamera();
    if (!grid_) throw std::runtime_error("Smoke: grid must be enabled by default");
    onMessage(WM_KEYDOWN, 'G', 0);
    if (grid_) throw std::runtime_error("Smoke: G did not hide grid");
    onMessage(WM_KEYDOWN, 'G', 0);
    if (!grid_) throw std::runtime_error("Smoke: G did not restore grid");
    auto bounds = rts::buildingBounds(game_, game_.buildings().front(), view_);
    click({bounds.x + bounds.width * .5f, bounds.y + bounds.height * .45f});
    if (!selectedBuilding()) throw std::runtime_error("Smoke: building selection failed");
    const rts::Cell rally{15, 22};
    focus(rts::center(rally));
    onMessage(WM_RBUTTONDOWN, 0, at(view_.project(rts::center(rally), 0)));
    if (game_.buildings().front().rally != rally) throw std::runtime_error("Smoke: rally command failed");
    resetCamera();
    const rts::BattleLayout layout(renderer_.size());
    click({layout.commands[0].x + 20, layout.commands[0].y + 20});
    if (game_.buildings().front().production.size() != 1) throw std::runtime_error("Smoke: production button failed");
    rts::Vec2 a{10000, 10000}, b{-10000, -10000};
    for (const auto& u : game_.units()) if (u.owner == game_.player().id && u.definition.canBuild) {
        const auto p = view_.project(u.position, game_.unitHeight(u));
        a.x = std::min(a.x, p.x - 28); a.y = std::min(a.y, p.y - 55);
        b.x = std::max(b.x, p.x + 28); b.y = std::max(b.y, p.y + 20);
    }
    onMessage(WM_LBUTTONDOWN, 0, at(a)); onMessage(WM_MOUSEMOVE, MK_LBUTTON, at(b)); onMessage(WM_LBUTTONUP, 0, at(b));
    if (ui_.selection.ids.size() != 6) throw std::runtime_error("Smoke: drag selection failed");
    onMessage(WM_KEYDOWN, 'B', 0);
    click(view_.project(rts::center({17, 17}), 0));
    if (game_.buildings().size() != 2) throw std::runtime_error("Smoke: construction placement failed");
    for (int tick = 0; tick < 700; ++tick) game_.tick();
    if (!game_.buildings().back().complete() || game_.units().size() != initialUnitCount + 1) throw std::runtime_error("Smoke: construction or training failed");
    bounds = rts::buildingBounds(game_, game_.buildings().back(), view_);
    click({bounds.x + bounds.width * .5f, bounds.y + 45});
    if (!selectedBuilding() || selectedBuilding()->definition.visual != rts::EntityVisual::Barracks)
        throw std::runtime_error("Smoke: barracks selection failed");
    onMessage(WM_KEYDOWN, 'Q', 0);
    const rts::Cell armyRally{21, 26};
    focus(rts::center(armyRally));
    onMessage(WM_RBUTTONDOWN, 0, at(view_.project(rts::center(armyRally), 0)));
    for (int tick = 0; tick < 750; ++tick) game_.tick();
    if (game_.units().size() != initialUnitCount + 2 || game_.units().back().cell != armyRally)
        throw std::runtime_error("Smoke: trained soldier did not reach rally");
    const rts::MinimapProjection mini(layout.minimap, game_.map());
    const rts::Cell miniRally{24, 27};
    onMessage(WM_RBUTTONDOWN, 0, at(mini.project(rts::center(miniRally))));
    // At this resolution a logical tile is about one pixel tall; compare with
    // the same pixel-centred picking used by the actual input handler.
    const auto picked = layout.minimapCell(mouse_, game_.map());
    if (!picked || !selectedBuilding() || selectedBuilding()->rally != *picked)
        throw std::runtime_error("Smoke: minimap rally projection failed");
    click(mini.project(rts::center({14, 15})));
    const auto focused = layout.minimapCell(mouse_, game_.map());
    const auto cameraCenter = view_.unproject({layout.world.width * .5f, layout.world.y + layout.world.height * .5f});
    if (!focused || std::hypot(cameraCenter.x - focused->x - .5f, cameraCenter.y - focused->y - .5f) > .01f)
        throw std::runtime_error("Smoke: minimap camera projection failed");
    auto arena = rts::loadScenario(paths_.asset(L"maps/demo.rtsmap"));
    arena.units = {{"human.soldier", 0, {15, 18}}, {"human.soldier", 1, {17, 19}}};
    game_ = rts::Simulation(std::move(arena), {}, definitions_.entity("human.worker"), definitions_.entities());
    ui_ = {}; resetCamera();
    onMessage(WM_KEYDOWN, VK_F1, 0);
    if (ui_.selection.ids.size() != 1 || game_.unit(ui_.selection.ids.front())->owner != game_.player().id)
        throw std::runtime_error("Smoke: F1 did not select only friendly soldiers");
    const auto fighter = ui_.selection.ids.front();
    const auto enemy = game_.units().back().id;
    const auto enemyBounds = rts::unitBounds(game_, *game_.unit(enemy), view_);
    onMessage(WM_RBUTTONDOWN, 0, at({enemyBounds.x + enemyBounds.width * .5f, enemyBounds.y + enemyBounds.height * .4f}));
    if (game_.unit(fighter)->targetUnit != enemy) throw std::runtime_error("Smoke: right click on enemy sprite did not attack");
    if (cursorKind() != rts::CursorKind::Attack) throw std::runtime_error("Smoke: attack cursor failed");
    for (int tick = 0; tick < 180 && game_.unit(enemy)->health == game_.unit(enemy)->definition.maximumHealth; ++tick) game_.tick();
    if (game_.unit(enemy)->health == game_.unit(enemy)->definition.maximumHealth) throw std::runtime_error("Smoke: ordered attack caused no damage");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"combat-preview.png");
    const auto combatUnitCount = game_.units().size();
    for (int tick = 0; tick < 600 && game_.units().size() == combatUnitCount; ++tick) game_.tick();
    ui_.selection.prune(game_);
    if (game_.units().size() == combatUnitCount || game_.corpses().empty()) throw std::runtime_error("Smoke: no unit died in combat");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"death-preview.png");
    auto heroScenario = rts::loadScenario(paths_.asset(L"maps/demo.rtsmap"));
    heroScenario.units = {{"human.hero", 0, {15, 18}}, {"human.soldier", 1, {16, 18}}};
    auto heroTypes = definitions_.entities();
    for (auto& type : heroTypes) if (type.id == "human.hero") { type.maximumHealth = 12; type.attackDamage = 0; }
    game_ = rts::Simulation(std::move(heroScenario), menu_.player, definitions_.entity("human.worker"), std::move(heroTypes));
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
}
}
