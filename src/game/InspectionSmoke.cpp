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
    if (rts::hoveredEntity(game_, preview, ui_, rts::BattleLayout(previewSize)) != enemy)
        throw std::runtime_error("Feedback: hovered enemy disagreed with selection picking");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"unit-hover-feedback-preview.png",
        nullptr, false, &ui_, rts::MenuPage::Main, 0, nullptr, &preview, previewSize);
    preview.origin = {};
    preview.origin = rts::Vec2{650, 460} - preview.project(rts::center(building.origin), 0);
    const auto hoverBounds = rts::buildingBounds(game_, building, preview);
    ui_.mouse = {hoverBounds.x + hoverBounds.width * .5f, hoverBounds.y + hoverBounds.height * .5f};
    if (rts::hoveredEntity(game_, preview, ui_, rts::BattleLayout(previewSize)) != building.id)
        throw std::runtime_error("Feedback: hovered building disagreed with selection picking");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"building-hover-preview.png",
        nullptr, false, &ui_, rts::MenuPage::Main, 0, nullptr, &preview, previewSize);

    rts::Scenario production{rts::Map(32, 32), {5, 5}, {12, 12}, {}};
    production.startingCrystals = 10000;
    game_ = rts::Simulation(std::move(production), menu_.player, definitions_.entity("human.worker"), definitions_.entities());
    auto& depot = const_cast<rts::Building&>(game_.buildings().front());
    depot.definition.trainableUnits = {"human.peacemaker", "human.slinger"};
    for (size_t i = 0; i < 10; ++i)
        if (!game_.train(depot.id, depot.definition.trainableUnits[i % 2]))
            throw std::runtime_error("Queue: could not fill ten mixed production slots");
    for (int i = 0; i < 30; ++i) game_.tick();
    ui_ = {}; ui_.selection.ids = {depot.id};
    for (const auto size : {rts::Vec2{960, 640}, rts::Vec2{1920, 1200}}) {
        preview.origin = {};
        preview.origin = rts::Vec2{size.x * .5f, 340} - preview.project(rts::center(depot.origin), 0);
        renderer_.snapshot(game_, rts::Paths::executable().parent_path() /
            (size.x < 1000 ? L"production-queue-compact.png" : L"production-queue-wide.png"),
            nullptr, false, &ui_, rts::MenuPage::Main, 0, nullptr, &preview, size);
    }
    const rts::ProductionQueueLayout queue(rts::SelectionPanelLayout(rts::BattleLayout(renderer_.size()).info).content);
    const auto queuePoint = [&](size_t index) {
        const auto slot = queue.slots[index];
        return rts::Vec2{slot.x + slot.width * .5f, slot.y + slot.height * .5f};
    };
    const int beforeForeign = game_.storedCrystals();
    depot.owner = 1;
    mouse_ = queuePoint(4);
    if (cursorKind() == rts::CursorKind::Hand) throw std::runtime_error("Queue: foreign icon has a clickable cursor");
    click(mouse_);
    if (depot.production.size() != 10 || game_.storedCrystals() != beforeForeign)
        throw std::runtime_error("Queue: click cancelled a foreign order");
    depot.owner = game_.player().id;
    for (size_t index : {size_t{4}, size_t{0}, size_t{7}}) {
        auto expected = depot.production;
        const auto job = expected[index]; expected.erase(expected.begin() + index);
        const int queueBalance = game_.storedCrystals(), supply = game_.armySupply().used();
        mouse_ = queuePoint(index);
        if (cursorKind() != rts::CursorKind::Hand) throw std::runtime_error("Queue: production icon lacks a clickable cursor");
        click(mouse_);
        if (depot.production.size() != expected.size() || game_.storedCrystals() != queueBalance + job.paidCrystals / 2 ||
            game_.armySupply().used() != supply - job.reservedSupply || ui_.selection.ids != std::vector{depot.id})
            throw std::runtime_error("Queue: icon click did not cancel exactly one job with half refund");
        for (size_t i = 0; i < expected.size(); ++i)
            if (depot.production[i].definitionId != expected[i].definitionId || depot.production[i].remainingTicks != expected[i].remainingTicks)
                throw std::runtime_error("Queue: icon click changed another job or its progress");
    }
    const int beforeEmpty = game_.storedCrystals();
    click(queuePoint(9));
    if (depot.production.size() != 7 || game_.storedCrystals() != beforeEmpty)
        throw std::runtime_error("Queue: empty slot changed production");
    const auto last = depot.production.back();
    onMessage(WM_KEYDOWN, 'X', 0);
    if (depot.production.size() != 6 || game_.storedCrystals() != beforeEmpty + last.paidCrystals / 2)
        throw std::runtime_error("Queue: X did not cancel the last order with half refund");

    const auto site = game_.construct(std::array{game_.worker().id}, "human.watchtower", {13, 12});
    if (!site) throw std::runtime_error("Cancellation: could not place construction fixture");
    const int beforeCancel = game_.storedCrystals();
    const int cost = game_.building(*site)->definition.cost.crystals;
    ui_.selection.ids = {*site};
    onMessage(WM_KEYDOWN, 'X', 0);
    if (game_.building(*site) || game_.storedCrystals() != beforeCancel + cost / 2 || !ui_.selection.ids.empty())
        throw std::runtime_error("Cancellation: construction hotkey failed to refund half and clear selection");
}
}
