#include "GameApplication.hpp"
#include <stdexcept>

namespace rts::game {
void GameApplication::exerciseUnitCommands() {
    rts::Scenario scene{rts::Map(40, 32), {3, 3}, {7, 9}, {{{12, 9}, 1000}}};
    scene.map.occupy({12, 9}); scene.extraWorkers = {{8, 9}};
    scene.units = {{"church.peacemaker", 0, {10, 10}}, {"church.catapult", 0, {11, 11}}, {"church.peacemaker", 1, {14, 11}},
        {"church.slinger", 0, {10, 12}}, {"church.peacemaker", 0, {9, 12}}};
    scene.heroSpawn = rts::Cell{9, 8};
    game_ = rts::Simulation(std::move(scene), menu_.player, definitions_.entity("church.worker"), definitions_.entities(), definitions_.startingDepot(definitions_.entity("church.worker").factionId).id, "church.hero");
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
    if (ui_.selection.ids != std::vector{second} || !layout.world.contains(view_.project(game_.unit(second)->position)))
        throw std::runtime_error("Commands: idle worker button did not cycle and focus");
    if (!(GetClassLongPtrW(window_, GCL_STYLE) & CS_DBLCLKS)) throw std::runtime_error("Selection: window does not receive double clicks");
    const auto workerBounds = rts::unitBounds(game_, *game_.unit(first), view_);
    const rts::Vec2 workerPoint{workerBounds.x + workerBounds.width * .5f, workerBounds.y + workerBounds.height * .5f};
    click(workerPoint);
    if (ui_.selection.ids != std::vector{first}) throw std::runtime_error("Selection: single click did not select one worker");
    onMessage(WM_LBUTTONDBLCLK, 0, at(workerPoint)); onMessage(WM_LBUTTONUP, 0, at(workerPoint));
    if (ui_.selection.ids != std::vector{first, second}) throw std::runtime_error("Selection: double click or its button-up lost matching workers");
    ui_.selection.ids = {soldier};
    onMessage(WM_LBUTTONDOWN, MK_SHIFT, at(workerPoint)); onMessage(WM_LBUTTONUP, MK_SHIFT, at(workerPoint));
    onMessage(WM_LBUTTONDBLCLK, MK_SHIFT, at(workerPoint)); onMessage(WM_LBUTTONUP, MK_SHIFT, at(workerPoint));
    if (ui_.selection.ids != std::vector{soldier, first, second}) throw std::runtime_error("Selection: Shift double click replaced or duplicated selection");
    ui_.selection.ids = {soldier}; focus({13, 11}, true);
    onMessage(WM_KEYDOWN, 'M', 0); click(view_.project(rts::center({17, 10}), 0));
    if (ui_.orderMode || rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::Move) throw std::runtime_error("Commands: M target failed");
    onMessage(WM_KEYDOWN, 'S', 0);
    if (rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::Stop) throw std::runtime_error("Commands: S failed");
    mouse_ = {layout.commands[3].x + 10, layout.commands[3].y + 10};
    if (cursorKind() != rts::CursorKind::Hand) throw std::runtime_error("Commands: raised top row did not show its button cursor");
    click(mouse_);
    if (rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::Hold) throw std::runtime_error("Commands: hold button failed");
    onMessage(WM_KEYDOWN, 'P', 0);
    const rts::MinimapProjection mini(layout.minimap, game_.map());
    click(mini.project(rts::center({18, 12})));
    if (ui_.orderMode || rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::Patrol) throw std::runtime_error("Commands: minimap patrol failed");
    onMessage(WM_KEYDOWN, 'A', 0);
    mouse_ = view_.project(rts::center({17, 10}), 0);
    if (cursorKind() != rts::CursorKind::Target) throw std::runtime_error("Commands: A did not show target reticle");
    click(mouse_);
    if (rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::AttackMove) throw std::runtime_error("Commands: A plus ground failed");
    onMessage(WM_KEYDOWN, 'A', 0);
    const auto enemyBounds = rts::unitBounds(game_, *game_.unit(enemy), view_);
    click({enemyBounds.x + enemyBounds.width * .5f, enemyBounds.y + enemyBounds.height * .5f});
    if (game_.unit(soldier)->attackTarget != enemy) throw std::runtime_error("Commands: A plus enemy failed");
    ui_.selection.ids = {first}; onMessage(WM_KEYDOWN, 'G', 0);
    const auto crystalBounds = rts::crystalBounds(game_, game_.crystals().front(), view_);
    click({crystalBounds.x + crystalBounds.width * .5f, crystalBounds.y + crystalBounds.height * .5f});
    if (game_.unit(first)->gatherOriginCrystal != 0) throw std::runtime_error("Commands: explicit gather failed");
    onMessage(WM_KEYDOWN, 'M', 0); onMessage(WM_RBUTTONDOWN, 0, at(view_.project(rts::center({17, 10}), 0)));
    if (ui_.orderMode || game_.unit(first)->gatherOriginCrystal != 0) throw std::runtime_error("Commands: right click did not cancel target mode");
    onMessage(WM_KEYDOWN, 'S', 0); onMessage(WM_KEYDOWN, 'B', 0); onMessage(WM_KEYDOWN, 'H', 0);
    if (ui_.placement != "church.hall") throw std::runtime_error("Commands: build submenu failed");
    onMessage(WM_KEYDOWN, VK_ESCAPE, 0);
    if (!ui_.placement.empty() || menu_.page != rts::MenuPage::Playing) throw std::runtime_error("Commands: Esc left targeting incorrectly");
    ui_.selection.ids = {siege}; onMessage(WM_KEYDOWN, 'T', 0);
    mouse_ = view_.project(rts::center({19, 16}), 0);
    if (cursorKind() != rts::CursorKind::Target) throw std::runtime_error("Commands: siege targeting did not show reticle");
    click(view_.project(rts::center({19, 16}), 0));
    if (ui_.orderMode || rts::unitOrder(*game_.unit(siege)) != rts::OrderKind::AttackGround) throw std::runtime_error("Commands: siege point order failed");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"unit-commands-preview.png", nullptr, false, &ui_);
    auto preview = ui_;
    const rts::BattleLayout previewLayout({1440, 900});
    preview.mouse = {previewLayout.idleWorker.x + 20, previewLayout.idleWorker.y + 20};
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"idle-worker-tooltip-preview.png", nullptr, false, &preview);
    preview.selection.ids = {first}; preview.buildMenu = true; preview.mouse = {-1, -1};
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"build-commands-preview.png", nullptr, false, &preview);

    const auto slinger = game_.units()[5].id, otherSoldier = game_.units()[6].id, hero = game_.hero()->id;
    ui_ = {}; ui_.selection.ids = {siege, first, slinger, hero, soldier, second, otherSoldier};
    const auto wholeSelection = ui_.selection.ids;
    const auto expectGroup = [&](const char* type, size_t members) {
        const auto group = ui_.selection.activeGroup(game_);
        if (group.type != type || group.ids.size() != members || ui_.selection.ids != wholeSelection)
            throw std::runtime_error("Commands: expected " + std::string(type) + "/" + std::to_string(members) +
                ", got " + group.type + "/" + std::to_string(group.ids.size()) +
                "; selected=" + std::to_string(ui_.selection.ids.size()) + "/" + std::to_string(wholeSelection.size()));
    };
    expectGroup("church.hero", 1);
    const auto portraits = rts::SelectionCards(game_, ui_.selection, layout.info);
    if (portraits.cards.front().id != hero || !portraits.cards.front().active)
        throw std::runtime_error("Commands: hero was not first in the portrait strip");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"active-hero-preview.png", nullptr, false, &ui_);
    const int manaBefore = game_.unit(hero)->mana;
    const auto* ability = rts::abilityCommandAt(game_, ui_, rts::firstAbilitySlot);
    if (!ability) throw std::runtime_error("Abilities: hero button missing");
    onMessage(WM_KEYDOWN, rts::abilityKeys[0], 0);
    if (ui_.orderMode || game_.unit(hero)->mana != manaBefore - ability->manaCost ||
        game_.unit(hero)->abilityCooldown(ability->id) != ability->cooldownTicks)
        throw std::runtime_error("Abilities: hotkey did not cast immediately");
    const auto abilityButton = layout.commands[rts::firstAbilitySlot];
    click({abilityButton.x + 10, abilityButton.y + 10});
    if (game_.unit(hero)->mana != manaBefore - ability->manaCost || ui_.orderMode)
        throw std::runtime_error("Abilities: cooldown button repeated the cast");
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"hero-ability-cooldown.png", nullptr, false, &ui_,
        rts::MenuPage::Main, 0, nullptr, nullptr, {800, 600});
    onMessage(WM_KEYDOWN, VK_TAB, 0); expectGroup("church.peacemaker", 2);
    for (size_t slot = 5; slot < rts::commandSlots; ++slot) {
        mouse_ = {layout.commands[slot].x + 10, layout.commands[slot].y + 10};
        if (cursorKind() != rts::CursorKind::Default) throw std::runtime_error("Commands: hidden slot retained a hand cursor");
        click(mouse_);
        if (ui_.orderMode || ui_.buildMenu) throw std::runtime_error("Commands: hidden slot executed an order");
    }
    for (unsigned key : {'G', 'T', 'B'}) {
        onMessage(WM_KEYDOWN, key, 0);
        if (ui_.orderMode || ui_.buildMenu) throw std::runtime_error("Commands: inactive type's hotkey executed an order");
    }
    const auto expectAll = [&](rts::OrderKind order) {
        for (const auto id : wholeSelection) if (rts::unitOrder(*game_.unit(id)) != order)
            throw std::runtime_error("Commands: general order reached only the active group");
    };
    for (const auto& [key, order] : std::array<std::pair<unsigned, rts::OrderKind>, 3>{{
        {'M', rts::OrderKind::Move}, {'P', rts::OrderKind::Patrol}, {'A', rts::OrderKind::AttackMove}}}) {
        onMessage(WM_KEYDOWN, key, 0); click(mini.project(rts::center({25, 15})));
        expectAll(order);
    }
    onMessage(WM_KEYDOWN, 'S', 0); expectAll(rts::OrderKind::Stop);
    click({layout.commands[3].x + 10, layout.commands[3].y + 10}); expectAll(rts::OrderKind::Hold);
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"active-soldiers-preview.png", nullptr, false, &ui_);
    onMessage(WM_KEYDOWN, VK_TAB, 0); expectGroup("church.slinger", 1);
    onMessage(WM_KEYDOWN, VK_TAB, 0); expectGroup("church.catapult", 1);
    onMessage(WM_KEYDOWN, 'T', 0); click(mini.project(rts::center({19, 16})));
    for (const auto id : wholeSelection) if (rts::unitOrder(*game_.unit(id)) !=
        (id == siege ? rts::OrderKind::AttackGround : rts::OrderKind::Hold))
        throw std::runtime_error("Commands: ground fire changed another group's order");
    onMessage(WM_KEYDOWN, VK_TAB, 0); expectGroup("church.worker", 2);
    onMessage(WM_KEYDOWN, 'G', 0); click(mini.project(rts::center({12, 9})));
    if (game_.unit(first)->gatherOriginCrystal != 0 || game_.unit(second)->gatherOriginCrystal != 0 ||
        rts::unitOrder(*game_.unit(soldier)) != rts::OrderKind::Hold)
        throw std::runtime_error("Commands: active workers did not gather separately");
    onMessage(WM_KEYDOWN, 'B', 0);
    click({layout.commands[rts::backCommandSlot].x + 10, layout.commands[rts::backCommandSlot].y + 10});
    if (ui_.buildMenu || !ui_.commandGroup.empty()) throw std::runtime_error("Commands: last-slot back button failed");
    onMessage(WM_KEYDOWN, 'B', 0); onMessage(WM_KEYDOWN, 'H', 0);
    onMessage(WM_KEYDOWN, VK_TAB, 0); expectGroup("church.hero", 1);
    if (!ui_.placement.empty() || ui_.orderMode || ui_.buildMenu || !ui_.commandGroup.empty())
        throw std::runtime_error("Commands: Tab retained the previous group's targeted command");
    for (const auto selected : {soldier, otherSoldier}) {
        ui_.selection = {}; ui_.selection.ids = wholeSelection;
        if (selected == otherSoldier) ui_.selection.activateGroup(game_, soldier);
        onMessage(WM_KEYDOWN, 'M', 0);
        const auto cards = rts::SelectionCards(game_, ui_.selection, layout.info);
        for (const auto& card : cards.cards) if (card.id == selected) {
            click({card.bounds.x + 10, card.bounds.y + 10}); break;
        }
        if (ui_.selection.ids != std::vector{selected} || ui_.selection.activeUnit(game_)->id != selected ||
            ui_.orderMode || !ui_.commandGroup.empty() || ui_.drag || mouseInWorld())
            throw std::runtime_error("Selection: icon did not isolate its unit or leaked a pending target click");
    }
    ui_.selection.ids = wholeSelection;
    ui_.selection.activateGroup(game_, soldier);
    expectGroup("church.peacemaker", 2);
    // SetKeyboardState only changes this UI thread's key state, without injecting system input.
    const auto numberKey = [&](unsigned key, bool control) {
        struct RestoreKeys {
            BYTE keys[256]{};
            RestoreKeys() { if (!GetKeyboardState(keys)) throw std::runtime_error("Cannot read keyboard state"); }
            ~RestoreKeys() { SetKeyboardState(keys); }
        } restore;
        BYTE keys[256]; std::copy(std::begin(restore.keys), std::end(restore.keys), keys);
        keys[VK_CONTROL] = keys[VK_LCONTROL] = control ? 0x80 : 0;
        keys[VK_RCONTROL] = 0;
        if (!SetKeyboardState(keys)) throw std::runtime_error("Cannot set test keyboard state");
        onMessage(WM_KEYDOWN, key, 0);
    };
    for (size_t slot = 0; slot < rts::controlGroupCount; ++slot) {
        numberKey(rts::controlGroupKey(slot), true);
        if (ui_.controlGroups.members(slot).size() != wholeSelection.size())
            throw std::runtime_error("Groups: Ctrl plus digit saved only the active type");
    }
    for (size_t slot = 0; slot < rts::controlGroupCount; ++slot) {
        ui_.selection.ids = {first};
        numberKey(rts::controlGroupKey(slot), false);
        if (!ui_.controlGroups.selected(slot, ui_.selection)) throw std::runtime_error("Groups: digit did not recall selection");
    }
    ui_.selection.ids = {first}; numberKey('2', true);
    if (ui_.controlGroups.members(0).size() != wholeSelection.size() || ui_.controlGroups.members(1) != std::vector{first})
        throw std::runtime_error("Groups: replacement changed another group");
    onMessage(WM_KEYDOWN, 'M', 0);
    click({layout.controlGroups[0].x + 10, layout.controlGroups[0].y + 10});
    if (ui_.orderMode || !ui_.controlGroups.selected(0, ui_.selection) || mouseInWorld() || cursorKind() != rts::CursorKind::Hand)
        throw std::runtime_error("Groups: button leaked a target click to the world");
    numberKey(VK_NUMPAD0, false);
    if (!ui_.controlGroups.selected(9, ui_.selection)) throw std::runtime_error("Groups: numpad zero failed");
    auto groupPreview = ui_; groupPreview.mouse = {-1, -1};
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"control-groups-preview.png", nullptr, false, &groupPreview);
    ui_.selection.ids = {first};
    const auto smallCards = rts::SelectionCards(game_, ui_.selection, layout.info);
    if (!smallCards.cards.empty()) throw std::runtime_error("Selection: single unit retained a group button");
    const auto portrait = rts::SelectionPanelLayout(layout.info).portrait;
    click({portrait.x + 10, portrait.y + 10});
    if (ui_.selection.ids != std::vector{first} || mouseInWorld() || cursorKind() != rts::CursorKind::Default)
        throw std::runtime_error("Selection: large portrait changed selection or behaved as a command");
    auto smallPreview = ui_; smallPreview.mouse = {-1, -1};
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"selection-small-preview.png", nullptr, false, &smallPreview,
        rts::MenuPage::BattleSetup, 0, nullptr, nullptr, {800, 600});

    ui_ = {}; ui_.selection.ids = {first, second};
    game_.stop(ui_.selection.ids);
    const auto* repair = rts::abilityCommandAt(game_, ui_, rts::firstAbilitySlot);
    if (!repair || !repair->repair || !repair->autocast) throw std::runtime_error("Repair: worker ability missing");
    const int repairId = repair->id;
    const rts::Vec2 buttonPoint{abilityButton.x + 28, abilityButton.y + 28};
    onMessage(WM_RBUTTONDOWN, 0, at(buttonPoint));
    if (rts::autocastState(game_, ui_, repairId) != rts::AutocastState::On || ui_.orderMode)
        throw std::runtime_error("Repair: right click did not enable group autocast");
    auto repairPreview = ui_;
    const rts::BattleLayout smallLayout({800, 600});
    repairPreview.mouse = {smallLayout.commands[rts::firstAbilitySlot].x + 28, smallLayout.commands[rts::firstAbilitySlot].y + 28};
    renderer_.snapshot(game_, rts::Paths::executable().parent_path() / L"repair-autocast-on.png", nullptr, false, &repairPreview,
        rts::MenuPage::Main, 0, nullptr, nullptr, {800, 600});
    game_.setAutocast(first, repairId, false);
    if (rts::autocastState(game_, ui_, repairId) != rts::AutocastState::Mixed) throw std::runtime_error("Repair: mixed group hidden");
    onMessage(WM_RBUTTONDOWN, 0, at(buttonPoint));
    if (rts::autocastState(game_, ui_, repairId) != rts::AutocastState::On) throw std::runtime_error("Repair: mixed toggle failed");
    onMessage(WM_RBUTTONDOWN, 0, at(buttonPoint));
    if (rts::autocastState(game_, ui_, repairId) != rts::AutocastState::Off) throw std::runtime_error("Repair: disabling group failed");
    auto& hall = const_cast<rts::Building&>(game_.buildings().front()); hall.health -= 100;
    onMessage(WM_KEYDOWN, 'Q', 0);
    if (ui_.pendingAbility != repairId || ui_.orderMode != rts::OrderKind::UseAbility) throw std::runtime_error("Repair: Q did not request target");
    onMessage(WM_KEYDOWN, VK_ESCAPE, 0);
    if (ui_.orderMode || ui_.pendingAbility) throw std::runtime_error("Repair: Escape retained targeting");
    click(buttonPoint);
    click(mini.project(rts::center(hall.origin)));
    for (auto id : ui_.selection.ids) if (game_.unit(id)->currentOrder.target != hall.id || game_.unit(id)->currentOrder.kind != rts::OrderKind::UseAbility)
        throw std::runtime_error("Repair: manual minimap target missed a worker");
    if (ui_.orderMode || ui_.pendingAbility) throw std::runtime_error("Repair: accepted target kept cursor mode");
    game_.stop(ui_.selection.ids);
    onMessage(WM_RBUTTONDOWN, 0, at(mini.project(rts::center(hall.origin))));
    for (auto id : ui_.selection.ids) if (game_.unit(id)->currentOrder.kind != rts::OrderKind::UseAbility ||
        game_.unit(id)->currentOrder.target != hall.id || game_.unit(id)->automaticAbility)
        throw std::runtime_error("Repair: contextual right click missed an eligible worker or toggled autocast");
    game_.stop(ui_.selection.ids); focus(rts::center(hall.origin), true);
    const auto repairHallBounds = rts::buildingBounds(game_, hall, view_);
    mouse_ = {repairHallBounds.x + repairHallBounds.width * .5f, repairHallBounds.y + repairHallBounds.height * .65f};
    if (cursorKind() != rts::CursorKind::Target) throw std::runtime_error("Repair: world cursor ignored contextual ability");
    onMessage(WM_RBUTTONDOWN, 0, at(mouse_));
    if (game_.unit(first)->currentOrder.target != hall.id || game_.unit(first)->currentOrder.kind != rts::OrderKind::UseAbility ||
        ui_.commandFeedback.target != hall.id) throw std::runtime_error("Repair: world right click missed building");

    rts::Scenario forcedScene{rts::Map(32,32), {4,4}, {10,10}, {}};
    forcedScene.units = {{"church.peacemaker",0,{14,10}}, {"church.peacemaker",0,{18,10}},
        {"church.flying_soldier",0,{14,14}}, {"church.slinger",0,{11,13}}};
    forcedScene.heroSpawn = rts::Cell{18,14};
    game_ = rts::Simulation(std::move(forcedScene), menu_.player, definitions_.entity("church.worker"), definitions_.entities(),
        definitions_.startingDepot("church").id, "church.hero");
    ui_ = {}; focus({14,12}, true);
    for (const auto& u : game_.units()) ui_.selection.ids.push_back(u.id);
    const auto repairer = game_.worker().id, fighter = game_.units()[1].id, friendTarget = game_.units()[2].id;
    const auto flyer = game_.units()[3].id, archer = game_.units()[4].id, friendHero = game_.hero()->id;
    ui_.selection.activateGroup(game_, repairer);
    const auto forceClickUnit = [&](rts::EntityId id) {
        onMessage(WM_KEYDOWN, 'S', 0); onMessage(WM_KEYDOWN, 'A', 0);
        if (ui_.orderMode != rts::OrderKind::AttackMove) throw std::runtime_error("Forced attack: worker active group hid selection-wide A");
        const auto bounds = rts::unitBounds(game_, *game_.unit(id), view_);
        mouse_ = {bounds.x + bounds.width * .5f, bounds.y + bounds.height * .4f};
        if (cursorKind() != rts::CursorKind::Target) throw std::runtime_error("Forced attack: ally cursor is blocked");
        click(mouse_);
        if (ui_.orderMode || ui_.commandFeedback.target != id) throw std::runtime_error("Forced attack: click did not select exact ally");
    };
    forceClickUnit(friendTarget);
    for (auto id : {fighter, archer, friendHero}) if (game_.unit(id)->attackTarget != friendTarget)
        throw std::runtime_error("Forced attack: capable selected member was skipped");
    for (auto id : {repairer, friendTarget, flyer}) if (game_.unit(id)->attackTarget || game_.unit(id)->currentOrder.kind != rts::OrderKind::Stop)
        throw std::runtime_error("Forced attack: worker, self or incompatible flyer received attack");
    forceClickUnit(friendHero);
    if (game_.unit(friendHero)->attackTarget || game_.unit(fighter)->attackTarget != friendHero)
        throw std::runtime_error("Forced attack: hero target or self filtering failed");
    game_.stop(ui_.selection.ids); focus({6,6}, true);
    onMessage(WM_KEYDOWN, 'A', 0);
    const auto& friendlyHall = game_.buildings().front();
    const auto hallBounds = rts::buildingBounds(game_, friendlyHall, view_);
    click({hallBounds.x + hallBounds.width * .5f, hallBounds.y + hallBounds.height * .65f});
    if (ui_.orderMode || game_.unit(fighter)->attackTarget != friendlyHall.id || game_.unit(archer)->attackTarget != friendlyHall.id ||
        game_.unit(repairer)->attackTarget || game_.unit(flyer)->attackTarget)
        throw std::runtime_error("Forced attack: friendly building target failed");
    game_.stop(ui_.selection.ids); ui_.selection.ids = {friendTarget}; focus({18,10}, true);
    onMessage(WM_KEYDOWN, 'A', 0);
    const auto selfBounds = rts::unitBounds(game_, *game_.unit(friendTarget), view_);
    mouse_ = {selfBounds.x + selfBounds.width * .5f, selfBounds.y + selfBounds.height * .4f};
    if (cursorKind() != rts::CursorKind::Blocked) throw std::runtime_error("Forced attack: self cursor is not blocked");
    click(mouse_);
    if (!ui_.orderMode || game_.unit(friendTarget)->currentOrder.kind != rts::OrderKind::Stop)
        throw std::runtime_error("Forced attack: invalid entity fell through to attack-move");
    onMessage(WM_KEYDOWN, VK_ESCAPE, 0);
    auto healing = *game_.unit(repairer)->ability(repairId);
    healing.id = 831; healing.targeting = rts::AbilityTargeting::FriendlyUnit;
    const_cast<rts::Unit&>(*game_.unit(fighter)).definition.abilities = {healing};
    const_cast<rts::Unit&>(*game_.unit(friendHero)).health -= 50;
    game_.stop(std::array{fighter, repairer}); ui_.selection.ids = {repairer, fighter};
    focus(game_.unit(friendHero)->position, true);
    const auto healBounds = rts::unitBounds(game_, *game_.unit(friendHero), view_);
    const rts::Vec2 healPoint{healBounds.x + healBounds.width * .5f, healBounds.y + healBounds.height * .4f};
    mouse_ = healPoint;
    if (cursorKind() != rts::CursorKind::Target) throw std::runtime_error("Repair: contextual unit cursor missing");
    onMessage(WM_RBUTTONDOWN, 0, at(healPoint));
    if (game_.unit(fighter)->currentOrder.kind != rts::OrderKind::UseAbility || game_.unit(fighter)->currentOrder.target != friendHero ||
        game_.unit(repairer)->currentOrder.kind != rts::OrderKind::Stop) throw std::runtime_error("Repair: unit right click is hardcoded to workers or buildings");
    game_.stop(std::array{fighter}); ui_.selection.ids = {fighter};
    onMessage(WM_KEYDOWN, 'Q', 0); click(healPoint);
    if (ui_.orderMode || game_.unit(fighter)->currentOrder.target != friendHero)
        throw std::runtime_error("Repair: manual unit targeting failed");
}
}
