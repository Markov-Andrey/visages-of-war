#include "GameApplication.hpp"
#include <stdexcept>

namespace rts::game {
void GameApplication::exerciseUnitCommands() {
    rts::Scenario scene{rts::Map(40, 32), {3, 3}, {7, 9}, {{{12, 9}, 1000}}};
    scene.map.occupy({12, 9}); scene.extraWorkers = {{8, 9}};
    scene.units = {{"human.soldier", 0, {10, 10}}, {"human.catapult", 0, {11, 11}}, {"human.soldier", 1, {14, 11}}};
    game_ = rts::Simulation(std::move(scene), menu_.player, definitions_.entity("human.worker"), definitions_.entities());
    ui_ = {}; menu_.page = rts::MenuPage::Playing; resetCamera();
    const auto first = game_.units()[0].id, second = game_.units()[1].id;
    const auto soldier = game_.units()[2].id, siege = game_.units()[3].id, enemy = game_.units()[4].id;
    const auto at = [&](rts::Vec2 point) {
        const float scale = GetDpiForWindow(window_) / 96.0f;
        return MAKELPARAM(int(std::lround(point.x * scale)), int(std::lround(point.y * scale)));
    };
    const auto click = [&](rts::Vec2 point) {
        onMessage(WM_LBUTTONDOWN, 0, at(point)); onMessage(WM_LBUTTONUP, 0, at(point));
    };
    onMessage(WM_KEYDOWN, VK_F8, 0);
    if (ui_.selection.ids != std::vector{first}) throw std::runtime_error("Commands: F8 did not select first free worker");
    const rts::BattleLayout layout(renderer_.size());
    click({layout.idleWorker.x + 10, layout.idleWorker.y + 10});
    for (int frame = 0; frame < 90; ++frame) camera_.update(view_,
        {layout.world.width * .5f, layout.world.y + layout.world.height * .5f}, {}, 1.0f / 60);
    const auto middle = view_.unproject({layout.world.width * .5f, layout.world.y + layout.world.height * .5f});
    if (ui_.selection.ids != std::vector{second} || std::hypot(middle.x - game_.unit(second)->position.x, middle.y - game_.unit(second)->position.y) > .01f)
        throw std::runtime_error("Commands: idle worker button did not cycle and focus");
    ui_.selection.ids = {soldier}; focus({13, 11}, true);
    onMessage(WM_KEYDOWN, 'M', 0); click(view_.project(rts::center({17, 10}), 0));
    if (ui_.orderMode || rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::Move) throw std::runtime_error("Commands: M target failed");
    onMessage(WM_KEYDOWN, 'S', 0);
    if (rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::Stop) throw std::runtime_error("Commands: S failed");
    click({layout.commands[3].x + 10, layout.commands[3].y + 10});
    if (rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::Hold) throw std::runtime_error("Commands: hold button failed");
    onMessage(WM_KEYDOWN, 'P', 0);
    const rts::MinimapProjection mini(layout.minimap, game_.map());
    click(mini.project(rts::center({18, 12})));
    if (ui_.orderMode || rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::Patrol) throw std::runtime_error("Commands: minimap patrol failed");
    onMessage(WM_KEYDOWN, 'A', 0); click(view_.project(rts::center({17, 10}), 0));
    if (rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::AttackMove) throw std::runtime_error("Commands: A plus ground failed");
    onMessage(WM_KEYDOWN, 'A', 0);
    const auto enemyBounds = rts::unitBounds(game_, *game_.unit(enemy), view_);
    click({enemyBounds.x + enemyBounds.width * .5f, enemyBounds.y + enemyBounds.height * .5f});
    if (game_.unit(soldier)->targetUnit != enemy) throw std::runtime_error("Commands: A plus enemy failed");
    ui_.selection.ids = {first}; onMessage(WM_KEYDOWN, 'G', 0);
    const auto crystalBounds = rts::crystalBounds(game_, game_.crystals().front(), view_);
    click({crystalBounds.x + crystalBounds.width * .5f, crystalBounds.y + crystalBounds.height * .5f});
    if (game_.unit(first)->gatherOriginCrystal != 0) throw std::runtime_error("Commands: explicit gather failed");
    onMessage(WM_KEYDOWN, 'M', 0); onMessage(WM_RBUTTONDOWN, 0, at(view_.project(rts::center({17, 10}), 0)));
    if (ui_.orderMode || game_.unit(first)->gatherOriginCrystal != 0) throw std::runtime_error("Commands: right click did not cancel target mode");
    onMessage(WM_KEYDOWN, 'S', 0); onMessage(WM_KEYDOWN, 'B', 0); onMessage(WM_KEYDOWN, 'H', 0);
    if (ui_.placement != "human.hall") throw std::runtime_error("Commands: build submenu failed");
    onMessage(WM_KEYDOWN, VK_ESCAPE, 0);
    if (!ui_.placement.empty() || menu_.page != rts::MenuPage::Playing) throw std::runtime_error("Commands: Esc left targeting incorrectly");
    ui_.selection.ids = {siege}; onMessage(WM_KEYDOWN, 'T', 0);
    click(view_.project(rts::center({19, 16}), 0));
    if (ui_.orderMode || rts::unitOrder(*game_.unit(siege)) != rts::OrderKind::AttackGround) throw std::runtime_error("Commands: siege point order failed");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"unit-commands-preview.png", nullptr, false, &ui_);
    auto preview = ui_;
    const rts::BattleLayout previewLayout({1440, 900});
    preview.mouse = {previewLayout.idleWorker.x + 20, previewLayout.idleWorker.y + 20};
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"idle-worker-tooltip-preview.png", nullptr, false, &preview);
    preview.selection.ids = {first}; preview.buildMenu = true; preview.mouse = {-1, -1};
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"build-commands-preview.png", nullptr, false, &preview);
}
}
