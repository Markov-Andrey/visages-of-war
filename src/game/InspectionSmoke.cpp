#include "GameApplication.hpp"
#include <stdexcept>

namespace rts::game {
void GameApplication::exerciseInspection() {
    rts::Scenario scene{rts::Map(40, 32), {3, 3}, {7, 7}, {}};
    scene.extraWorkers = {{8, 7}};
    scene.units = {{"human.hero", 1, {11, 8}}, {"human.worker", rts::neutralPlayer, {10, 11}}, {"human.peacemaker", 0, {7, 10}}};
    scene.crystals.push_back({{12, 11}, 1000}); scene.map.occupy({12, 11});
    game_ = rts::Simulation(std::move(scene), menu_.player, definitions_.entity("human.worker"), definitions_.entities());
    ui_ = {}; menu_.page = rts::MenuPage::Playing; clearCommandMode();
    const rts::BattleLayout layout(renderer_.size());
    const auto at = [&](rts::Vec2 point) {
        const float scale = GetDpiForWindow(window_) / 96.0f;
        return MAKELPARAM(int(std::lround(point.x * scale)), int(std::lround(point.y * scale)));
    };
    const auto click = [&](rts::Vec2 point, WPARAM modifiers = 0) {
        onMessage(WM_LBUTTONDOWN, modifiers, at(point)); onMessage(WM_LBUTTONUP, modifiers, at(point));
    };
    const auto rejectControls = [&] {
        const auto message = game_.message();
        for (unsigned key : {'M', 'A', 'S', 'P', 'H', 'G', 'B', 'Q', 'W', 'E', 'R', 'T', 'X'}) onMessage(WM_KEYDOWN, key, 0);
        for (const auto button : layout.commands) {
            mouse_ = {button.x + 10, button.y + 10};
            if (cursorKind() != rts::CursorKind::Default) throw std::runtime_error("Inspection: foreign command has a hand cursor");
            click(mouse_);
        }
        const rts::Vec2 ground{layout.world.width * .5f, layout.world.y + layout.world.height * .5f};
        onMessage(WM_RBUTTONDOWN, 0, at(ground));
        if (ui_.orderMode || ui_.rallyMode || ui_.buildMenu || !ui_.placement.empty() || game_.message() != message)
            throw std::runtime_error("Inspection: input activated foreign controls");
    };
    for (size_t index : {size_t{2}, size_t{3}}) {
        const auto& unit = game_.units()[index];
        focus(unit.position, true);
        const auto bounds = rts::unitBounds(game_, unit, view_);
        const rts::Vec2 point{bounds.x + bounds.width * .5f, bounds.y + bounds.height * .5f};
        ui_.selection.ids = {game_.worker().id}; click(point, MK_SHIFT);
        if (ui_.selection.ids != std::vector{unit.id} || ui_.selection.inspectedUnit(game_) != &unit || ui_.selection.activeUnit(game_))
            throw std::runtime_error("Inspection: foreign unit was not selected singly");
        onMessage(WM_LBUTTONDBLCLK, MK_SHIFT, at(point)); onMessage(WM_LBUTTONUP, MK_SHIFT, at(point));
        if (ui_.selection.ids != std::vector{unit.id}) throw std::runtime_error("Inspection: double click changed foreign selection");
        const int mana = unit.mana; const auto state = unit.state;
        rejectControls();
        if (unit.mana != mana || unit.state != state || !unit.abilityCooldowns.empty()) throw std::runtime_error("Inspection: foreign unit accepted a command");
        renderer_.snapshot(game_, rts::Paths::executable().parent_path() /
            (index == 2 ? L"enemy-inspection-preview.png" : L"neutral-inspection-preview.png"), nullptr, false, &ui_);
    }
    auto& building = const_cast<rts::Building&>(game_.buildings().front());
    building.owner = 1;
    focus(rts::center(building.origin), true);
    const auto bounds = rts::buildingBounds(game_, building, view_);
    click({bounds.x + bounds.width * .5f, bounds.y + bounds.height * .5f});
    if (ui_.selection.ids != std::vector{building.id} || selectedBuilding()) throw std::runtime_error("Inspection: enemy building became controllable");
    const auto rally = building.rally;
    const auto balance = game_.storedCrystals();
    rejectControls();
    if (building.rally != rally || !building.production.empty() || game_.storedCrystals() != balance)
        throw std::runtime_error("Inspection: enemy building accepted a command");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"enemy-building-inspection-preview.png", nullptr, false, &ui_);

    const auto enemy = game_.units()[2].id, soldier = game_.units().back().id;
    ui_.selection.ids = {soldier};
    rightClick(game_.unit(enemy)->cell);
    if (ui_.commandFeedback.target != enemy || game_.unit(soldier)->targetUnit != enemy)
        throw std::runtime_error("Feedback: accepted right-click attack did not mark its actual target");
    const auto now = rts::CommandFeedback::Clock::now();
    const auto tick = game_.clock().elapsedTicks();
    if (ui_.commandFeedback.age(now) < 0 || ui_.commandFeedback.age(now + std::chrono::seconds(1)) >= 0 || game_.clock().elapsedTicks() != tick)
        throw std::runtime_error("Feedback: confirmation did not expire while simulation was paused");
    ui_.commandFeedback = {};
    rightClick(game_.crystals().front().cell);
    if (ui_.commandFeedback.target) throw std::runtime_error("Feedback: non-worker gathering produced a confirmation");
    ui_.selection.ids = {enemy}; rightClick(game_.worker().cell);
    if (ui_.commandFeedback.target) throw std::runtime_error("Feedback: inspected enemy produced a command confirmation");
    ui_.selection.ids = {game_.worker().id}; rightClick(game_.crystals().front().cell);
    if (ui_.commandFeedback.target != game_.crystals().front().id || game_.worker().gatherOriginCrystal != 0)
        throw std::runtime_error("Feedback: accepted gathering did not mark the resource");
    ui_.selection.ids = {soldier, game_.worker().id}; rightClick(game_.crystals().front().cell);
    if (ui_.commandFeedback.target != game_.crystals().front().id)
        throw std::runtime_error("Feedback: mixed gathering confirmed an ineligible soldier's old attack");
    ui_.selection.ids = {game_.worker().id};
    const rts::Vec2 previewSize{1280, 900};
    rts::WorldView preview{{}, .95f};
    preview.origin = rts::Vec2{650, 340} - preview.project(game_.unit(enemy)->position, game_.unitHeight(*game_.unit(enemy)));
    const auto enemyBounds = rts::unitBounds(game_, *game_.unit(enemy), preview);
    ui_.mouse = {enemyBounds.x + enemyBounds.width * .5f, enemyBounds.y + enemyBounds.height * .5f};
    if (rts::hoveredUnit(game_, preview, ui_, rts::BattleLayout(previewSize)) != enemy)
        throw std::runtime_error("Feedback: hovered enemy disagreed with selection picking");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"unit-hover-feedback-preview.png",
        nullptr, false, &ui_, rts::MenuPage::Main, 0, nullptr, &preview, previewSize);
}
}
