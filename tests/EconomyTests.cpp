#include "TestSupport.hpp"

namespace rts::tests {
namespace {
EntityDefinition repairWorker() {
    auto worker = fixtureWorker(); worker.attackDamage = 0; worker.sprite.work = 9;
    AbilityDefinition ability{17, "Repair", "", 0, 0};
    ability.targeting = AbilityTargeting::Building;
    ability.rightClick = true;
    ability.animation = AbilityAnimation::Work;
    ability.repair = RepairDefinition{};
    ability.autocast = AutocastDefinition{8, 1, false};
    worker.abilities.insert(worker.abilities.begin(), ability);
    return worker;
}
Scenario repairScene() {
    Scenario scene{Map(30, 24), {3, 3}, {3, 5}, {}};
    scene.slots[0].startingCrystals = 1000;
    return scene;
}
}
void economyTests(TestSuite& test, const TestContext& context) {
    const auto& assets=context.assets;
    test("Entity abilities share mana cooldown autocast and UI on buildings and units", [] {
        auto worker = fixtureWorker(); worker.maximumMana = 60;
        AbilityDefinition ability{921, "Common cast", "", 7, 5};
        ability.animation = AbilityAnimation::Attack;
        ability.autocast = AutocastDefinition{8, 1, false};
        worker.abilities = {ability};
        auto depot = fixtureDepot(worker); depot.maximumMana = 60; depot.abilities = {ability};
        depot.hero = HeroDefinition{};
        auto game = makeTestSimulation(repairScene(), {}, worker, {worker, depot});
        const auto buildingId = game.buildings().front().id, unitId = game.worker().id;
        for (auto id : {buildingId, unitId}) {
            require(game.entity(id)->mana == 60 && game.activateAbility(id, ability.id), "Common cast rejected");
            require(game.entity(id)->mana == 53 && game.entity(id)->abilityCooldown(ability.id) == 5 &&
                game.entity(id)->abilityAnimation.current(), "Common activation state missing");
            require(!game.activateAbility(id, ability.id), "Cooldown bypassed");
            require(!game.activateAbilityFor(1, id, ability.id), "Foreign owner cast accepted");
            require(game.setAutocast(id, ability.id, true), "Non-repair autocast rejected");
        }
        ticks(game, 5);
        require(game.entity(buildingId)->mana == 46 && game.entity(unitId)->mana == 46, "Building and unit clocks diverged");
        GameplayUi ui; ui.selection.ids = {buildingId};
        require(abilityCommandAt(game, ui, firstAbilitySlot) && unitCommandVisible(game, ui, firstAbilitySlot) &&
            autocastState(game, ui, ability.id) == AutocastState::On, "Building ability UI unavailable");
        require(toggleAutocast(game, ui, firstAbilitySlot) && !game.entity(buildingId)->autocastEnabled(ability.id), "Building toggle failed");
        ui.commandGroup = depot.id;
        require(commandRecipients(game, ui, OrderKind::UseAbility) == std::vector<EntityId>{buildingId}, "Building target command lost its recipient");
        require(game.grantExperience(buildingId, 100) && game.entity(buildingId)->level() == 2 &&
            game.entity(buildingId)->maximumHealth() == depot.maximumHealth + 40, "Progression is still locked to circle entities");
    });
    test("Entity target filters allow healthy allies enemies and self independently of effects", [] {
        auto worker = fixtureWorker(); worker.maximumMana = 50;
        AbilityDefinition ability{932, "Targeted animation", "", 3, 10};
        ability.targeting = AbilityTargeting::Entity; ability.range = 12; ability.rightClick = true;
        ability.animation = AbilityAnimation::Work;
        worker.abilities = {ability};
        auto scene = repairScene(); scene.units = {{worker.id, 1, {8, 5}}, {worker.id, neutralPlayer, {7, 7}}};
        auto game = makeTestSimulation(scene, {}, worker);
        const auto own = game.worker().id, enemy = game.units()[1].id, neutral = game.units()[2].id;
        require(game.canTargetAbility(game.worker(), ability, own) && game.canTargetAbility(game.worker(), ability, game.buildings()[0].id),
            "Healthy allied targets still require repair");
        require(!game.canTargetAbility(game.worker(), ability, enemy), "Own filter accepted enemy");
        ability.targets.relations = {TargetRelation::Enemy, TargetRelation::Neutral};
        require(game.canTargetAbility(game.worker(), ability, enemy) && game.canTargetAbility(game.worker(), ability, neutral) &&
            !game.canTargetAbility(game.worker(), ability, own), "Relation filter ignored");
        ability.targets.relations = {TargetRelation::Own}; ability.targets.allowSelf = false;
        require(!game.canTargetAbility(game.worker(), ability, own), "Self filter ignored");
        ability.targets.allowSelf = true; ability.targets.damagedOnly = true;
        require(!game.canTargetAbility(game.worker(), ability, own), "State filter ignored");
        const_cast<Entity&>(game.worker()).health--;
        require(game.canTargetAbility(game.worker(), ability, own), "Damaged target rejected");
        require(game.command(std::array{own}, game.worker().cell, own), "Generic right click requires repair");
        game.tick();
        require(game.worker().mana == 47 && game.worker().currentOrder.kind == OrderKind::Stop &&
            game.worker().abilityAnimation.current(), "Instant targeted lifecycle failed");
    });
    test("Buildings cast repair and harvest using the same lifecycle without moving", [] {
        auto worker = fixtureWorker(); worker.maximumHealth = 100;
        auto repair = repairWorker().abilities.front(); repair.targeting = AbilityTargeting::Entity;
        repair.range = 5; repair.manaCost = 3; repair.cooldownTicks = 10;
        auto harvest = *worker.gatherAbility(); harvest.id = 947; harvest.harvest = HarvestDefinition{HarvestCredit::Direct, 1, 2};
        harvest.range = 5; harvest.manaCost = 4; harvest.cooldownTicks = 10;
        auto depot = fixtureDepot(worker); depot.abilities = {repair, harvest}; depot.maximumMana = 40;
        auto scene = repairScene(); scene.crystals = {{{7, 4}, 7}}; scene.map.occupy({7, 4});
        auto game = makeTestSimulation(scene, {}, worker, {worker, depot});
        const auto id = game.buildings().front().id;
        const Vec2 original = game.entity(id)->position;
        const_cast<Entity&>(game.worker()).health = 70;
        require(game.activateAbility(id, repair.id, game.worker().id), "Building cannot restore unit");
        ticks(game, 5);
        require(game.worker().health > 70 && game.entity(id)->mana == 37, "Building repair did not execute");
        game.stop(std::array{id});
        require(game.entity(id)->currentOrder.kind == OrderKind::Stop, "Building cast cannot be cancelled");
        const auto balance = game.storedCrystals();
        require(game.activateAbility(id, harvest.id, game.crystals().front().id), "Building harvest rejected");
        ticks(game, 5);
        require(game.storedCrystals() == balance + 7 && game.crystals().front().remaining == 0 &&
            game.entity(id)->mana == 33 && game.entity(id)->position == original, "Building harvest lost resources or moved");
    });
    test("Several harvest abilities keep independent costs and the selected effect through deliveries", [] {
        auto worker = gatheringWorker(); worker.maximumMana = 50; worker.carryCapacity = 3;
        auto cargo = *worker.gatherAbility(); cargo.manaCost = 5; cargo.cooldownTicks = 10000;
        cargo.harvest->intervalTicks = 1; cargo.harvest->amount = 3;
        auto direct = cargo; direct.id = 958; direct.manaCost = 7; direct.harvest->credit = HarvestCredit::Direct;
        worker.abilities = {cargo, direct};
        auto game = makeTestSimulation(flatScenario(11), {}, worker);
        const auto id = game.worker().id, node = game.crystals().front().id;
        require(game.activateAbility(id, cargo.id, node), "Cargo activation rejected");
        ticks(game, 1000);
        require(game.storedCrystals() == 11 && game.worker().mana == 45 && game.worker().abilityCooldown(cargo.id) == 9000,
            "Delivery loop charged activation again or lost cargo");
        auto other = makeTestSimulation(flatScenario(11), {}, worker);
        require(other.activateAbility(other.worker().id, direct.id, other.crystals().front().id), "Second harvest activation rejected");
        ticks(other, 300);
        require(other.storedCrystals() == 11 && other.worker().cargo == 0 && other.worker().mana == 43 &&
            other.worker().abilityCooldown(cargo.id) == 0, "Selected harvest effect or cooldown leaked across abilities");
    });
    test("Stationary circle entities retain unit targeting and reject out of range casts without charges", [] {
        auto worker = fixtureWorker(); worker.mobile = false; worker.maximumMana = 20;
        auto repair = repairWorker().abilities.front(); repair.range = 1; repair.manaCost = 5;
        worker.abilities = {repair};
        auto scene = repairScene(); scene.worker = {20, 18};
        auto game = makeTestSimulation(scene, {}, worker);
        const auto id = game.worker().id, b = game.buildings().front().id;
        const_cast<Building&>(game.buildings().front()).health--;
        game.revealMap();
        require(!game.activateAbility(id, repair.id, b) && game.worker().mana == 20 && game.worker().route.empty(),
            "Stationary caster moved or charged an unreachable cast");
        AbilityDefinition target; target.targeting = AbilityTargeting::Unit;
        require(game.canTargetAbility(game.worker(), target, id), "Mobility changed collision category");
        require(!game.order(std::array{id}, OrderKind::Move, {19,18}), "Stationary entity accepted movement");
    });
    test("Harvest autocast shares activation costs and distinguishes automatic cancellation from manual orders", [] {
        auto worker = gatheringWorker(); worker.maximumMana = 40;
        auto& ability = *worker.gatherAbility(); ability.id = 967; ability.manaCost = 4; ability.cooldownTicks = 5;
        ability.autocast = AutocastDefinition{12, 1, true};
        auto game = makeTestSimulation(flatScenario(100), {}, worker);
        const auto id = game.worker().id;
        game.tick();
        require(game.worker().automaticAbility && game.worker().currentOrder.abilityId == 967 && game.worker().mana == 36,
            "Harvest autocast did not use common activation");
        require(game.setAutocast(id, 967, false) && game.worker().currentOrder.kind == OrderKind::Stop && !game.worker().repeatGather,
            "Disabling harvest autocast did not cancel its approach");
        ticks(game, 5);
        require(game.activateAbility(id, 967, game.crystals().front().id), "Manual harvest after autocast rejected");
        require(game.setAutocast(id, 967, true) && game.setAutocast(id, 967, false) &&
            game.worker().currentOrder.kind == OrderKind::Gather && game.worker().mana == 32,
            "Autocast toggle cancelled or charged a manual harvest");
    });
    test("Footprint contact abilities respect cliffs while authored ranged abilities use their range", [] {
        auto worker = fixtureWorker();
        auto repair = repairWorker().abilities.front(); repair.targeting = AbilityTargeting::Entity;
        auto depot = fixtureDepot(worker); depot.abilities = {repair};
        auto scene = repairScene(); scene.map.at(scene.worker).height = -1;
        auto game = makeTestSimulation(scene, {}, worker, {worker, depot}); game.revealMap();
        const_cast<Entity&>(game.worker()).health--;
        const auto id = game.buildings().front().id;
        require(!game.activateAbility(id, repair.id, game.worker().id), "Contact effect crossed a shore cliff");
        const_cast<Entity&>(*game.entity(id)).definition.abilities.front().range = 2;
        require(game.activateAbility(id, repair.id, game.worker().id), "Explicit ranged ability inherited contact terrain restrictions");
    });
    test("Contextual repair follows abilities and readiness across a mixed selection", [] {
        auto type = repairWorker(); type.id = "test.restorer"; type.canBuild = false; std::erase_if(type.abilities, [](const auto& a) { return a.harvest.has_value(); });
        type.abilities[0].id = 317;
        auto scene = repairScene(); scene.extraWorkers = {{4, 5}, {6, 5}};
        auto game = makeTestSimulation(std::move(scene), {}, type);
        const auto first = game.units()[0].id, second = game.units()[1].id, third = game.units()[2].id;
        auto& secondUnit = const_cast<Unit&>(*game.unit(second)); secondUnit.definition.abilities.clear();
        auto& thirdUnit = const_cast<Unit&>(*game.unit(third)); thirdUnit.abilityCooldowns.push_back({317, 50});
        const std::array ids{first, second, third, first};
        game.order(ids, OrderKind::Hold);
        auto& target = const_cast<Building&>(game.buildings().front()); target.health = 500;
        require(game.command(ids, target.origin, target.id), "Contextual repair rejected non-worker ability holder");
        require(game.unit(first)->currentOrder.kind == OrderKind::UseAbility && game.unit(first)->currentOrder.abilityId == 317 &&
            !game.unit(first)->automaticAbility, "Context order depends on unit or ability ID or enables autocast");
        require(game.unit(second)->currentOrder.kind == OrderKind::Hold && game.unit(third)->currentOrder.kind == OrderKind::Hold,
            "Ineligible mixed selection members lost their orders");
        auto& caster = const_cast<Unit&>(*game.unit(first));
        caster.definition.abilities[0].rightClick = false;
        require(!game.contextualAbility(caster, target.id), "Disabled right-click flag ignored");
        caster.definition.abilities[0].rightClick = true; caster.definition.abilities[0].manaCost = 1; caster.mana = 0;
        require(!game.contextualAbility(caster, target.id), "Context cast ignores mana");
        caster.definition.abilities[0].manaCost = 0; target.health = target.definition.maximumHealth;
        require(!game.contextualAbility(caster, target.id), "Full target accepts contextual restoration");
    });
    test("Unit restoration follows target filters, production cost and moving targets", [] {
        auto type = repairWorker(); type.abilities[0].targeting = AbilityTargeting::Unit;
        type.maximumHealth = 100; type.trainingTicks = 200; type.cost = {120, 0};
        auto scene = repairScene(); scene.extraWorkers = {{9, 8}};
        auto game = makeTestSimulation(std::move(scene), {}, type);
        const auto caster = game.worker().id, recipient = game.units()[1].id;
        auto& target = const_cast<Unit&>(*game.unit(recipient)); target.health = 50;
        require(!game.canTargetAbility(game.worker(), type.abilities[0], game.buildings().front().id), "Unit filter accepts buildings");
        auto buildingOnly = type.abilities[0]; buildingOnly.targeting = AbilityTargeting::Building;
        require(!game.canTargetAbility(game.worker(), buildingOnly, recipient), "Building filter accepts units");
        target.owner = 1; require(!game.canTargetAbility(game.worker(), type.abilities[0], recipient), "Restoration accepts enemy unit"); target.owner = 0;
        require(game.command(std::array{caster}, target.cell, recipient), "Right click did not restore unit");
        game.order(std::array{recipient}, OrderKind::Move, {15, 8});
        game.tick(); require(target.health == 50, "Restoration heals remotely");
        for (int i = 0; i < 600 && target.health < target.maximumHealth(); ++i) {
            game.tick(); require(separated(game.worker(), target), "Restoration passed through its target");
        }
        require(target.health == 100 && game.storedCrystals() == 970, "Moving unit not restored at proportional training price");
        game.stop(std::array{recipient}); target.health = 90;
        game.setAutocast(caster, 17, true); game.stop(std::array{caster});
        ticks(game, 150);
        require(target.health == 100 && game.storedCrystals() == 964, "Autocast ignores unit targets");
        target.health = 80;
        require(game.activateAbility(caster, 17, recipient), "Manual unit restoration rejected");
        target.health = 0; game.tick();
        require(game.worker().currentOrder.kind == OrderKind::Stop, "Dead unit kept restoration active");
    });
    test("Repair time and proportional cost derive from ability and building data", [] {
        const auto worker = repairWorker(); auto depot = fixtureDepot(worker); depot.constructionTicks = 400;
        auto game = makeTestSimulation(repairScene(), {}, worker, {worker, depot});
        auto& building = const_cast<Building&>(game.buildings().front()); building.health = 600;
        const auto id = game.worker().id;
        require(game.activateAbility(id, 17, building.id), "Manual repair rejected");
        game.tick();
        require(game.worker().state == UnitState::UsingAbility && unitFrame(game.worker()).column == 9, "Repair does not use work pose");
        ticks(game, 118);
        require(building.health == 996 && game.storedCrystals() == 960, "Repair speed or proportional price wrong");
        game.tick();
        require(building.health == 1000 && game.storedCrystals() == 960 && game.worker().currentOrder.kind == OrderKind::Stop,
            "Repair did not finish at 75% construction time scaled by missing health");
        require(unitFrame(game.worker()).column != 9 && !game.activateAbility(id, 17, building.id), "Full building keeps work pose or accepts repair");
    });
    test("Repair credit survives repeated cancellation and workers share one exact price", [] {
        const auto worker = repairWorker(); auto depot = fixtureDepot(worker); depot.cost.crystals = 101;
        auto scene = repairScene(); scene.extraWorkers = {{4, 5}};
        auto game = makeTestSimulation(std::move(scene), {}, worker, {worker, depot});
        auto& building = const_cast<Building&>(game.buildings().front());
        for (int i = 0; i < 100; ++i) {
            building.health = 999;
            const auto id = game.units()[i % 2].id;
            require(game.activateAbility(id, 17, building.id), "Repeated repair rejected");
            game.tick(); game.stop(std::array{id});
            require(building.health == 1000, "One health repair failed");
        }
        require(game.storedCrystals() == 994, "Repair rounded each order separately or gave free healing");
        building.health = 600;
        for (const auto& unit : game.units()) require(game.activateAbility(unit.id, 17, building.id), "Shared repair rejected");
        ticks(game, 45);
        require(building.health == 1000 && game.storedCrystals() == 974, "Workers did not combine speed or share fractional cost");
    });
    test("Repair pauses without resources and rejects invalid targets without changing orders", [] {
        auto scene = repairScene(); scene.slots[0].startingCrystals = 1;
        auto game = makeTestSimulation(std::move(scene), {}, repairWorker());
        auto& building = const_cast<Building&>(game.buildings().front()); building.health = 600;
        const auto id = game.worker().id;
        require(game.activateAbility(id, 17, building.id), "Repair rejected"); ticks(game, 30);
        require(building.health == 610 && game.storedCrystals() == 0 && !game.worker().abilityWorking &&
            unitFrame(game.worker()).column != 9, "Insufficient balance healed freely or retained work pose");
        for (int invalid = 0; invalid < 4; ++invalid) {
            building.owner = invalid == 0 ? 1 : 0;
            building.health = invalid == 1 ? 0 : invalid == 2 ? 1000 : 600;
            building.constructionProgress = invalid == 3 ? 0 : building.definition.constructionTicks;
            require(!game.activateAbility(id, 17, building.id), "Invalid building accepted");
        }
        require(!game.activateAbilityFor(1, id, 17, building.id) && !game.setAutocastFor(1, id, 17, true) &&
            !game.activateAbility(id, 999, building.id) && !game.setAutocast(id, 999, true), "Ability bypassed ownership or catalog");
        building.owner = 0; building.health = 600; building.constructionProgress = building.definition.constructionTicks;
        game.order(std::array{id}, OrderKind::Hold);
        require(!game.activateAbility(id, 17, 999999) && game.worker().currentOrder.kind == OrderKind::Hold,
            "Rejected target interrupted current order");
    });
    test("Autocast respects Stop, radius and explicit orders; disabling cancels only automatic repair", [] {
        auto scene = repairScene(); scene.worker = {12, 5}; scene.crystals = {{{14, 9}, 1000}};
        scene.map.occupy({14, 9});
        auto worker = repairWorker(); worker.abilities[0].autocast->radius = 2;
        auto game = makeTestSimulation(std::move(scene), {}, worker);
        auto& building = const_cast<Building&>(game.buildings().front()); building.health = 500;
        const auto id = game.worker().id;
        require(!game.worker().autocastEnabled(17) && game.setAutocast(id, 17, true), "Autocast default or toggle wrong");
        ticks(game, 3); require(game.worker().currentOrder.kind == OrderKind::Stop, "Autocast ignored radius");
        auto& unit = const_cast<Unit&>(game.worker()); unit.definition.abilities[0].autocast->radius = 30;
        for (auto order : {OrderKind::Hold, OrderKind::Move, OrderKind::Gather}) {
            require(game.order(std::array{id}, order, order == OrderKind::Gather ? Cell{14, 9} : Cell{12, 6}), "Fixture order rejected");
            ticks(game, 3); require(game.worker().currentOrder.kind == order && building.health == 500, "Autocast stole explicit order");
        }
        game.stop(std::array{id}); game.tick();
        require(game.worker().automaticAbility && game.worker().state == UnitState::ToAbility && building.health == 500, "Idle autocast failed or healed remotely");
        require(game.setAutocast(id, 17, false) && game.worker().currentOrder.kind == OrderKind::Stop, "Disabling left automatic approach running");
        require(game.activateAbility(id, 17, building.id), "Manual approach failed");
        game.setAutocast(id, 17, true); game.setAutocast(id, 17, false);
        require(game.worker().currentOrder.kind == OrderKind::UseAbility && !game.worker().automaticAbility, "Toggle cancelled manual repair");
        ticks(game, 500); require(building.health == 1000, "Worker never reached and repaired target");
    });
    test("Repair cannot cross cliffs or a solid surround and stops when target is destroyed", [] {
        auto scene = repairScene(); scene.worker = {7, 5};
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 6; ++x) scene.map.at({x, y}).height = 1;
        auto game = makeTestSimulation(std::move(scene), {}, repairWorker());
        auto& building = const_cast<Building&>(game.buildings().front()); building.health = 500;
        require(!game.activateAbility(game.worker().id, 17, building.id), "Repair crossed impassable cliff");
        game.setAutocast(game.worker().id, 17, true); ticks(game, 4);
        require(building.health == 500 && game.worker().currentOrder.kind == OrderKind::Stop, "Autocast repairs unreachable cliff target");
        auto surrounded = repairScene(); surrounded.worker = {12, 10};
        for (Cell delta : std::array<Cell, 8>{{{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}}})
            surrounded.extraWorkers.push_back(surrounded.worker + delta);
        auto trapped = makeTestSimulation(std::move(surrounded), {}, repairWorker());
        auto& target = const_cast<Building&>(trapped.buildings().front()); target.health = 500;
        const Vec2 position = trapped.worker().position;
        require(trapped.activateAbility(trapped.worker().id, 17, target.id), "Traffic should allow queued approach");
        for (int tick = 0; tick < 90; ++tick) {
            trapped.tick();
            require(target.health == 500, "Surrounded worker healed remotely");
            require(groundLengthSquared(trapped.worker().position - position) < .5f, "Repair escaped solid surround");
            for (size_t i = 1; i < trapped.units().size(); ++i) {
                require(separated(trapped.worker(), trapped.units()[i]), "Repair crossed a surrounding body");
                require(trapped.units()[i].position == center(trapped.units()[i].cell), "Repair pushed a surrounding body");
            }
        }
        target.health = 0; trapped.tick();
        require(trapped.worker().currentOrder.kind == OrderKind::Stop, "Destroyed repair target kept order alive");
    });
    test("Autocast group controls expose mixed state and keep other active groups unchanged", [] {
        auto worker = repairWorker(); worker.abilities[0].autocast->enabledByDefault = true;
        auto scene = repairScene(); scene.extraWorkers = {{4, 5}};
        auto game = makeTestSimulation(std::move(scene), {}, worker);
        GameplayUi ui; for (const auto& unit : game.units()) ui.selection.ids.push_back(unit.id);
        require(autocastState(game, ui, 17) == AutocastState::On, "Catalog default not applied on spawn");
        game.setAutocast(game.worker().id, 17, false);
        require(autocastState(game, ui, 17) == AutocastState::Mixed, "Mixed state hidden");
        require(toggleAutocast(game, ui, firstAbilitySlot) && autocastState(game, ui, 17) == AutocastState::On, "Mixed toggle did not enable group");
        require(toggleAutocast(game, ui, firstAbilitySlot) && autocastState(game, ui, 17) == AutocastState::Off, "Group toggle did not disable");
        ui.commandGroup = "different.type";
        require(commandRecipients(game, ui, OrderKind::UseAbility).empty(), "Targeted ability survived incompatible active group");
        ui.buildMenu = true; require(!toggleAutocast(game, ui, firstAbilitySlot), "Hidden ability accepted toggle");
    });
    test("Ten player economies reserve supply independently and reject inactive or invalid owners", [] {
        Scenario scene{Map(40, 24), {1, 1}, {4, 3}, {}};
        auto worker = fixtureWorker(); worker.cost.supply = 100;
        for (PlayerId id = 0; id < maximumPlayers; ++id) {
            scene.slots[id] = {id == 9 ? PlayerSlotState::Player : PlayerSlotState::AI, 100 + id};
            if (id != 9) scene.units.push_back({worker.id, id, {8 + 2 * id, 10}});
        }
        auto game = makeTestSimulation(scene, {9}, worker);
        require(game.worker().owner == 9 && game.buildings().front().owner == 9 && game.storedCrystals() == 109,
            "Nonzero local player inherited player zero's entities or funds");
        for (PlayerId id = 0; id < maximumPlayers; ++id)
            require(game.storedCrystals(id) == 100 + id && game.armySupply(id).used() == 100, "Player economies share resources or supply");
        require(!game.train(game.buildings().front().id) && game.storedCrystals() == 109, "Full supply charged a rejected order");
        auto over = scene; over.units.push_back({worker.id, 3, {35, 18}});
        mustThrow([&] { makeTestSimulation(over, {9}, worker); });
        for (auto state : {PlayerSlotState::Empty, PlayerSlotState::Closed}) {
            auto invalid = scene; invalid.slots[3] = {state, 0};
            mustThrow([&] { makeTestSimulation(invalid, {9}, worker); });
        }
        scene.units.front().owner = 10;
        mustThrow([&] { makeTestSimulation(scene, {9}, worker); });
        require(!game.orderFor(10, std::array{game.worker().id}, OrderKind::Stop), "Invalid actor accepted");
    });
    test("Direct and delivered harvest credit only the owner's balance", [] {
        for (const auto credit : {HarvestCredit::Direct, HarvestCredit::Cargo}) {
            Scenario scene{Map(32, 24), {1, 1}, {5, 5}, {{{8, 5}, 7}, {{23, 15}, 11}}};
            for (const auto& node : scene.crystals) scene.map.occupy(node.cell);
            scene.slots[0].startingCrystals = 200; scene.slots[1].startingCrystals = 400;
            auto worker = gatheringWorker(); worker.gatherAbility()->harvest = {credit, 1, 3};
            worker.trainingTicks = 3;
            scene.units = {{worker.id, 1, {20, 15}}};
            auto depot = fixtureDepot(worker); depot.constructionTicks = 1;
            auto game = makeTestSimulation(scene, {}, worker, {worker, depot});
            const auto own = game.worker().id, other = game.units().back().id;
            const auto aiDepot = game.constructFor(1, std::array{other}, depot.id, {18, 12});
            require(aiDepot.has_value(), "AI owner cannot build its depot");
            for (int i = 0; i < 400 && !game.building(*aiDepot)->complete(); ++i) game.tick();
            require(game.building(*aiDepot)->complete(), "AI depot never completed");
            require(!game.order(std::array{other}, OrderKind::Gather, {23, 15}), "Local commands controlled foreign workers");
            require(game.order(std::array{own}, OrderKind::Gather, {8, 5}) &&
                game.orderFor(1, std::array{other}, OrderKind::Gather, {23, 15}), "Harvest order rejected");
            for (int i = 0; i < 800 && (game.storedCrystals(0) != 207 || game.storedCrystals(1) != 211); ++i) game.tick();
            require(game.storedCrystals(0) == 207 && game.storedCrystals(1) == 211 &&
                game.crystals()[0].remaining == 0 && game.crystals()[1].remaining == 0,
                "Harvest credited the wrong player or lost resources");
        }
    });
    test("Foreign production refunds and destruction never change the local economy", [] {
        Scenario scene{Map(32, 24), {1, 1}, {5, 5}, {}};
        scene.slots[0].startingCrystals = 500; scene.slots[1].startingCrystals = 700;
        auto worker = gatheringWorker(); worker.cost = {75, 2}; worker.trainingTicks = 3;
        scene.units = {{worker.id, 1, {20, 15}}};
        auto depot = fixtureDepot(worker); depot.cost = {101, 3}; depot.constructionTicks = 1;
        auto game = makeTestSimulation(scene, {}, worker, {worker, depot});
        const auto other = game.units().back().id;
        auto site = game.constructFor(1, std::array{other}, depot.id, {18, 12});
        require(site && !game.cancelConstruction(*site) && game.cancelConstructionFor(1, *site), "Foreign construction cancellation ownership failed");
        require(game.storedCrystals(1) == 649 && game.armySupply(1).used() == 2, "Construction refund or supply is wrong");
        site = game.constructFor(1, std::array{other}, depot.id, {18, 12});
        require(site.has_value(), "Cannot rebuild cancelled construction");
        for (int i = 0; i < 400 && !game.building(*site)->complete(); ++i) game.tick();
        require(game.building(*site)->complete(), "Foreign production building incomplete");
        require(!game.train(*site) && game.trainFor(1, *site), "Training ownership failed");
        require(!game.cancelTraining(*site) && game.cancelTrainingFor(1, *site), "Queue cancellation ownership failed");
        require(game.storedCrystals(1) == 510 && game.armySupply(1).used() == 5, "Queue refund credited wrong owner");
        require(game.trainFor(1, *site), "Cannot queue replacement");
        ticks(game, 4);
        const auto produced = game.units().back().id;
        require(game.units().back().owner == 1 && produced != other && game.armySupply(1).used() == 7,
            "Production changed owner or reserved supply twice");
        require(game.trainFor(1, *site) && game.trainFor(1, *site), "Cannot fill destruction queue");
        const auto balance = game.storedCrystals(1);
        const_cast<Building*>(game.building(*site))->health = 0;
        const_cast<Unit*>(game.unit(produced))->health = 0;
        game.tick();
        require(!game.building(*site) && !game.unit(produced) && game.armySupply(1).used() == 2 && game.storedCrystals(1) == balance,
            "Foreign destruction refunded crystals or retained supply");
        ticks(game, 5);
        require(game.storedCrystals(0) == 500 && game.armySupply(0).used() == 5 && game.armySupply(1).used() == 2,
            "Foreign transactions changed the local economy or released supply twice");
        require(!game.trainFor(2, game.buildings().front().id) && !game.cancelTrainingFor(neutralPlayer, game.buildings().front().id),
            "Inactive slots accepted economic commands");
    });

    test("Novice credits crystals in its work pose without cargo or a route to the depot", [&] {
        const auto definitions = Definitions::load(assets / "data/catalog.json");
        const auto type = definitions.entity("church.worker");
        require(type.canGather() && type.carryCapacity == 0 && type.harvesting().credit == HarvestCredit::Direct,
            "Novice is not a direct gatherer");
        auto s = flatScenario(); s.worker = {6,7};
        for (int y = 0; y < s.map.height(); ++y) s.map.at({4,y}).blocked = true;
        auto game = makeTestSimulation(std::move(s), {}, type);
        require(commandCapable(game.worker(), UnitCommand::Gather) && game.command({7,7}), "Zero-cargo worker cannot gather");
        for (int i = 0; i < 100 && game.worker().state != UnitState::Harvesting; ++i) game.tick();
        require(game.worker().state == UnitState::Harvesting, "Novice did not reach the node");
        const auto position = game.worker().position;
        const int interval = type.harvesting().intervalTicks;
        require(interval > HarvestDefinition{}.intervalTicks, "Direct mining must be slower than default cargo mining");
        for (int i = 1; i < interval; ++i) {
            game.tick();
            require(game.storedCrystals() == 0 && unitFrame(game.worker()).column == type.sprite.work,
                "Harvest credited early or lost the work pose");
        }
        game.tick();
        require(game.storedCrystals() == 7 && game.worker().cargo == 0 && game.worker().position == position,
            "Direct harvest accumulated cargo or visited the depot");
        const auto events = game.takeEvents();
        const auto income = std::find_if(events.begin(), events.end(), [](const auto& event) { return std::holds_alternative<CrystalsHarvested>(event); });
        require(income != events.end() && std::get<CrystalsHarvested>(*income).amount == 7 &&
            std::get<CrystalsHarvested>(*income).owner == game.worker().owner, "Income event lost its amount or recipient");
        ticks(game, interval * 3);
        require(game.storedCrystals() == 23 && game.crystals()[0].remaining == 0 && game.worker().cargo == 0 &&
            game.worker().state == UnitState::Idle && game.map().walkable({7,7}), "Direct harvesting lost resources or failed to finish");
        const auto finalEvents = game.takeEvents();
        std::vector<int> amounts;
        for (const auto& event : finalEvents) if (const auto* credit = std::get_if<CrystalsHarvested>(&event)) amounts.push_back(credit->amount);
        require(amounts == std::vector<int>{7, 7, 2}, "Final partial batch reported more than it credited");
    });
    test("Direct harvest batches clamp to the reserve and Stop resets progress", [&] {
        const auto definitions = Definitions::load(assets / "data/catalog.json");
        auto type = definitions.entity("church.worker");
        type.gatherAbility()->harvest->intervalTicks = 4;
        type.gatherAbility()->harvest->amount = 3;
        auto s = flatScenario(7); s.worker = {6,7};
        auto game = makeTestSimulation(std::move(s), {}, type);
        const auto start = [&] {
            require(game.command({7,7}), "Gather rejected");
            for (int i = 0; i < 100 && game.worker().state != UnitState::Harvesting; ++i) game.tick();
            require(game.worker().state == UnitState::Harvesting, "Worker did not start harvesting");
        };
        start(); ticks(game, 2); game.stop(); ticks(game, 12);
        require(game.storedCrystals() == 0 && game.crystals()[0].remaining == 7 &&
            unitFrame(game.worker()).column == type.sprite.idle, "Stop retained gathering or work animation");
        start(); ticks(game, 3);
        require(game.storedCrystals() == 0, "New order retained cancelled harvest progress");
        game.tick(); require(game.storedCrystals() == 3, "Configured batch amount ignored");
        ticks(game, 4); require(game.storedCrystals() == 6, "Second batch failed");
        ticks(game, 4); require(game.storedCrystals() == 7 && game.worker().cargo == 0 && game.map().walkable({7,7}),
            "Final partial batch duplicated or lost crystals");
    });
    test("Catalog cargo harvesting still fills capacity and credits only on delivery", [&] {
        const auto definitions = Definitions::load(assets / "data/catalog.json");
        auto type = definitions.entity("church.worker");
        *type.gatherAbility() = definitions.ability(3); type.carryCapacity = 10;
        type.gatherAbility()->harvest->amount = 6;
        auto s = flatScenario(); s.worker = {6,7};
        auto game = makeTestSimulation(std::move(s), {}, type);
        game.command({7,7});
        for (int i = 0; i < 100 && game.worker().state != UnitState::Harvesting; ++i) game.tick();
        const int interval = type.harvesting().intervalTicks;
        ticks(game, interval);
        require(game.worker().cargo == 6 && game.storedCrystals() == 0, "Cargo harvest credited immediately");
        ticks(game, interval);
        require(game.worker().cargo == 10 && game.storedCrystals() == 0 && game.crystals()[0].remaining == 13,
            "Batch exceeded cargo capacity or lost surplus crystals");
        ticks(game, 3000);
        require(game.storedCrystals() == 23 && game.worker().cargo == 0, "Catalog cargo mode lost its final partial delivery");
    });
    test("Direct group gathering conserves resources and stays near the clicked deposit", [&] {
        const auto definitions = Definitions::load(assets / "data/catalog.json");
        auto type = definitions.entity("church.worker"); type.dayVision = type.nightVision = 32;
        auto s = gatheringScenario(); s.crystals[0].remaining = 7; s.crystals[2].remaining = 9;
        s.extraWorkers = {{4,7},{4,9},{3,8}};
        auto game = makeTestSimulation(std::move(s), {}, type);
        std::vector<EntityId> ids; for (const auto& u : game.units()) ids.push_back(u.id);
        require(game.command(ids, {15,8}), "Direct group gather rejected");
        for (int i = 0; i < 2200; ++i) {
            game.tick();
            auto total = game.storedCrystals(); for (const auto& node : game.crystals()) total += node.remaining;
            for (const auto& u : game.units()) require(u.cargo == 0 && u.state != UnitState::ToHall &&
                u.gatherOriginCrystal == 0 && u.targetCrystal != 1, "Direct gathering returned cargo or drifted to another field");
            require(total == 1016, "Concurrent harvesting lost or duplicated reserves");
        }
        require(game.storedCrystals() == 16 && game.crystals()[1].remaining == 1000, "Local direct gathering did not finish");
    });
    test("Construction rejects a neighbouring circle crossing its square footprint", [] {
        rts::Scenario s{rts::Map(20, 16), {1, 1}, {6, 6}, {}};
        auto game = makeTestSimulation(std::move(s));
        game.command({9, 6});
        for (int tick = 0; tick < 60 && game.worker().position.x < 7.75f; ++tick) game.tick();
        game.stop();
        require(game.worker().cell == rts::Cell{7, 6}, "Test worker entered the construction cell");
        require(!game.canPlace("church.hall", {8, 6}), "Building overlapped the edge of a neighbouring circle");
        require(game.canPlace("church.hall", {9, 6}), "Non-overlapping circle blocked construction");
    });
    test("Large crystals accept clicks on every cell and release the entire footprint after harvesting", [&] {
        const auto& world = context.worldAssets;
        for (const auto* kind : {"crystal.medium", "crystal.big"}) {
            Scenario s{Map(24, 24), {1, 1}, {16, 13}, {world.instantiateCrystal(kind, {13, 13})}};
            s.crystals.front().remaining = 2;
            const auto node = s.crystals.front();
            rebuildScenario(s, {3, 2});
            auto worker = gatheringWorker(); worker.dayVision = worker.nightVision = 3;
            auto game = makeTestSimulation(std::move(s), {}, worker);
            const auto id = game.crystals().front().id;
            require(!game.fog().visible(node.cell) && game.crystalVisible(game.crystals().front()), "Partial footprint visibility lost");
            for (int y = 0; y < node.height; ++y) for (int x = 0; x < node.width; ++x)
                require(game.command(node.cell + Cell{x, y}), "A crystal footprint cell rejected gathering");
            const auto slots = perimeter(game.map(), node.cell, node.width, node.height);
            const Cell arrival = game.worker().route.empty() ? game.worker().cell : game.worker().route.back();
            require(std::find(slots.begin(), slots.end(), arrival) != slots.end(), "Worker did not approach the outer perimeter");
            ticks(game, 1800);
            require(game.crystals().front().remaining == 0 && game.storedCrystals() == 2 && game.crystals().front().id == id, "Depletion lost cargo or resource identity");
            for (int y = 0; y < node.height; ++y) for (int x = 0; x < node.width; ++x)
                require(game.map().walkable(node.cell + Cell{x, y}), "Depleted footprint still blocks movement");
        }
    });
    test("Explicit gathering crosses temporary traffic instead of switching to a nearer field", [] {
        auto s = gatheringScenario();
        for (int y = 0; y < 16; ++y) if (y != 8) s.map.at({8, y}).blocked = true;
        s.extraWorkers = {{8, 8}};
        auto game = makeTestSimulation(std::move(s), {}, gatheringWorker());
        const auto blocker = game.units()[1].id;
        require(game.command({15, 8}), "Gather command rejected");
        require(game.worker().targetCrystal == 0, "Temporary traffic redirected gathering to another field");
        ticks(game, 210);
        require(game.worker().targetCrystal == 0 && game.crystals()[1].remaining == 1000, "Waiting worker abandoned clicked deposit");
        game.command(std::span<const rts::EntityId>(&blocker, 1), {10, 11});
        ticks(game, 1600);
        require(game.crystals()[0].remaining < 1000 && game.storedCrystals() > 0, "Gathering did not resume after traffic cleared");
        require(game.crystals()[1].remaining == 1000 && game.crystals()[2].remaining == 1000, "Worker changed deposit after delivery");
    });
    test("Full gathering slots select a neighbor of the clicked node and retain that area after delivery", [] {
        auto s = gatheringScenario();
        s.extraWorkers = {{14, 8}, {16, 8}, {15, 7}, {15, 9}};
        auto game = makeTestSimulation(std::move(s), {}, gatheringWorker());
        game.command({15, 8});
        require(game.worker().targetCrystal == 2, "Chose worker-nearest crystal instead of target-nearest neighbor");
        ticks(game, 2200);
        require(game.storedCrystals() >= 20 && game.crystals()[2].remaining < 1000, "Neighbor gathering failed to repeat after depositing");
        require(game.crystals()[1].remaining == 1000, "Delivery lost the original gathering area");
    });
    test("Saturated resource field waits for a slot without selecting a remote crystal", [] {
        auto s = gatheringScenario(false);
        s.extraWorkers = {{14, 8}, {16, 8}, {15, 7}, {15, 9}};
        auto game = makeTestSimulation(std::move(s), {}, gatheringWorker());
        const auto blocker = game.units()[1].id;
        game.command({15, 8}); ticks(game, 450);
        require(game.worker().targetCrystal == 0 && game.worker().repeatGather, "Full deposit lost its gather order");
        require(game.crystals()[1].remaining == 1000, "Saturation sent worker to a remote deposit");
        game.command(std::span<const rts::EntityId>(&blocker, 1), {12, 6});
        ticks(game, 1500);
        require(game.crystals()[0].remaining < 1000 && game.storedCrystals() > 0, "Freed gather slot was not used");
    });
    test("Depletion stays near original gather target and never chains into another field", [] {
        auto s = gatheringScenario();
        s.crystals[0].remaining = 3; s.crystals[2].remaining = 4;
        s.crystals.push_back({{20, 8}, 1000}); s.map.occupy({20, 8});
        auto game = makeTestSimulation(std::move(s), {}, gatheringWorker());
        game.command({15, 8}); ticks(game, 5000);
        require(game.storedCrystals() == 7 && game.worker().cargo == 0, "Did not finish original local deposits exactly");
        require(game.crystals()[1].remaining == 1000 && game.crystals()[3].remaining == 1000, "Gathering drifted beyond the original area");
        require(game.worker().state == rts::UnitState::Idle, "Exhausted area never finished");
    });
    test("Blocked gathering retains intent until terrain opens and Stop cancels retry", [] {
        auto s = gatheringScenario(false);
        // An observer beyond the barrier keeps the requested deposit visible;
        // this test isolates blocked movement from the new forest sight rule.
        s.extraWorkers.push_back({14, 6});
        for (int y = 0; y < 16; ++y) if (y != 8) s.map.at({8, y}).blocked = true;
        auto tree = rts::makeEnvironment("TREE", 777, {8, 8});
        s.environment.push_back(tree); s.map.occupy(tree.origin);
        auto game = makeTestSimulation(std::move(s), {}, gatheringWorker());
        require(game.fog().visible({15, 8}), "Observer did not reveal the gathering target");
        game.command({15, 8}); ticks(game, 180);
        require(game.worker().repeatGather && game.crystals()[1].remaining == 1000, "Static obstruction redirected gather order");
        game.damageEnvironment(tree.id, tree.maximumHitPoints); ticks(game, 1500);
        require(game.crystals()[0].remaining < 1000 && game.crystals()[1].remaining == 1000, "Gather did not resume when terrain opened");
        game.stop(); ticks(game, 30);
        const int remaining = game.crystals()[0].remaining;
        ticks(game, 180);
        require(game.worker().state == rts::UnitState::Idle && !game.worker().repeatGather && game.crystals()[0].remaining == remaining, "Stop did not cancel gather retry");
    });
    test("Gather ordered mid-step retains the clicked area when the crystal depletes", [] {
        auto s = gatheringScenario();
        s.worker = {14, 10}; s.extraWorkers = {{15, 7}}; s.crystals[0].remaining = 1;
        auto type = gatheringWorker(); type.movementPerSecond = .5f;
        auto game = makeTestSimulation(std::move(s), {}, type);
        const auto other = game.units()[1].id;
        game.command(std::span<const rts::EntityId>(&other, 1), {15, 8});
        game.command({14, 9}); ticks(game, 1);
        require(!game.worker().route.empty() && game.worker().position != rts::center(game.worker().cell), "Test did not begin a movement step");
        game.command({15, 8});
        require(game.worker().gatherOriginCrystal == 0, "Gather did not retain the clicked origin");
        ticks(game, 350);
        require(game.crystals()[0].remaining == 0, "Other worker did not deplete clicked deposit");
        require(game.worker().gatherOriginCrystal == 0 && game.worker().targetCrystal == 2, "Replacement gather lost depleted target's area");
        require(game.crystals()[1].remaining == 1000 && game.crystals()[2].remaining < 1000, "Replacement gather selected wrong field");
    });
    test("Unreachable deposit preserves carried resources", [] {
        auto scenario = flatScenario(4); scenario.worker = {5, 3};
        for (int y = 0; y < 10; ++y) scenario.map.at({4, y}).blocked = true;
        auto game = makeTestSimulation(std::move(scenario));
        game.command({7, 7});
        ticks(game, 1200);
        require(game.storedCrystals() == 0 && game.worker().cargo == 4, "Cargo lost on failed return path");
        require(game.worker().state == rts::UnitState::Idle, "Unreachable hall retry loop");
    });
    test("Construction validates site, charges once, resumes and releases only its footprint", [&] {
        auto definitions = rts::Definitions::load(assets / "data/catalog.json");
        auto types = definitions.entities();
        for (auto& type : types) type.constructionMode = ConstructionMode::Worker;
        auto annex = definitions.entity("church.hall"); annex.id = "test.annex";
        annex.width = annex.height = 1; annex.cost = {75, 0}; annex.constructionMode = ConstructionMode::Worker;
        types.push_back(annex);

        auto s = flatScenario(); s.slots[0].startingCrystals = 500;
        rts::Simulation game(std::move(s), {}, definitions.entity("church.worker"), std::move(types), definitions.startingDepot(definitions.entity("church.worker").factionId).id);
        const std::array<rts::EntityId, 1> ids{game.worker().id};
        require(!game.construct(ids, "church.barracks", {4, 3}), "Built on worker");
        require(!game.construct(ids, "church.barracks", {8, 8}), "Built outside map");
        require(!game.construct(ids, "church.barracks", {6, 6}), "Built over crystal");
        require(game.storedCrystals() == 500, "Invalid placement consumed crystals");
        const auto building = game.construct(ids, "church.barracks", {5, 3});
        require(building && !game.map().walkable({5, 3}) && game.storedCrystals() == 400, "Foundation/cost wrong");
        require(!game.train(*building), "Unfinished building produced unit");
        ticks(game, 30); game.stop(ids);
        const int progress = game.building(*building)->constructionProgress;
        ticks(game, 60);
        require(progress > 0 && game.building(*building)->constructionProgress == progress, "Construction advanced without builder");
        const_cast<Building&>(*game.building(*building)).health -= 10;
        game.command(ids, {5, 3}); ticks(game, 300);
        require(game.building(*building)->complete(), "Resumed construction never finished");
        require(game.building(*building)->health == definitions.entity("church.barracks").maximumHealth - 10,
            "Construction completion healed damage instead of preserving it");
        const auto extra = game.construct(ids, annex.id, {3, 5});
        require(extra && game.storedCrystals() == 325, "Extra construction placement failed");
        require(game.cancelConstruction(*extra), "Cancellation failed");
        require(game.map().walkable({3, 5}) && game.storedCrystals() == 362, "Cancellation did not refund half rounded down or release occupancy");
        require(!game.map().walkable({5, 3}) && !game.cancelConstruction(*building), "Cancellation touched completed building");
    });
    test("Summoning requires initiation, frees the worker and preserves duration and damage", [&] {
        const auto definitions = Definitions::load(assets / "data/catalog.json");
        auto s = flatScenario(); s.slots[0].startingCrystals = 500;
        Simulation game(std::move(s), {}, definitions.entity("church.worker"), definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id);
        const std::array ids{game.worker().id};
        const auto site = game.construct(ids, "church.barracks", {5, 3});
        require(site.has_value(), "Summoning site rejected");
        game.stop(ids); ticks(game, 200);
        require(game.building(*site)->constructionProgress == 0 && game.building(*site)->health == 1,
            "Summoning started before the worker reached the site");
        game.command(ids, {5, 3});
        for (int i = 0; i < 100 && game.building(*site)->constructionProgress == 0; ++i) game.tick();
        require(game.building(*site)->constructionProgress == 1, "Initiation skipped or advanced construction twice");
        require(game.worker().state == UnitState::Idle && game.worker().targetBuilding == 0 &&
            game.worker().currentOrder.kind == OrderKind::Stop, "Summoning retained the builder");
        const auto start = game.worker().position;
        require(game.command(ids, {4, 8}), "Freed worker could not take a new order");
        ticks(game, 30);
        require(groundLengthSquared(game.worker().position - start) > .1f && game.building(*site)->constructionProgress == 31,
            "Summoning depends on worker position or advances at the wrong rate");
        const_cast<Building&>(*game.building(*site)).health -= 10;
        game.command(ids, {5, 3}); ticks(game, 1);
        require(game.building(*site)->constructionProgress == 32 && game.worker().targetBuilding == 0,
            "Reassigning a worker restarted or accelerated summoning");
        const_cast<Unit&>(game.worker()).health = 0;
        game.tick();
        require(game.units().empty(), "Worker death fixture failed");
        const int duration = game.building(*site)->definition.constructionTicks;
        ticks(game, duration - 34);
        require(!game.building(*site)->complete() && game.building(*site)->constructionProgress == duration - 1,
            "Summoning finished early or stopped after worker death");
        game.tick();
        require(game.building(*site)->complete() && game.building(*site)->health == definitions.entity("church.barracks").maximumHealth - 10,
            "Summoning did not finish exactly or healed combat damage");
        require(game.storedCrystals() == 400 && !game.cancelConstruction(*site), "Completion changed cost or allowed a refund");
        auto events = game.takeEvents();
        require(std::count_if(events.begin(), events.end(), [&](const auto& event) {
            const auto* finished = std::get_if<ConstructionFinished>(&event); return finished && finished->building == *site;
        }) == 1, "Summoning completion event missing or duplicated");
        ticks(game, 10); events = game.takeEvents();
        require(std::none_of(events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<ConstructionFinished>(event);
        }), "Completed summoning emitted repeated events");
    });
    test("Summoning cancellation before initiation refunds half and destruction never refunds", [&] {
        const auto definitions = Definitions::load(assets / "data/catalog.json");
        auto s = flatScenario(); s.slots[0].startingCrystals = 500;
        Simulation game(std::move(s), {}, definitions.entity("church.worker"), definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id);
        const std::array ids{game.worker().id};
        const auto pending = game.construct(ids, "church.barracks", {5, 3});
        require(pending && game.cancelConstruction(*pending) && game.storedCrystals() == 450 && game.map().walkable({5, 3}),
            "Uninitiated summoning did not refund half or release occupancy");
        const auto site = game.construct(ids, "church.barracks", {5, 3});
        require(site.has_value(), "Could not reuse cancelled site");
        ticks(game, 30);
        require(game.building(*site)->constructionProgress > 0, "Destruction fixture did not start summoning");
        const_cast<Building&>(*game.building(*site)).health = 0;
        game.tick();
        require(!game.building(*site) && game.storedCrystals() == 350 && game.map().walkable({5, 3}),
            "Summoning revived a destroyed building or refunded its cost");
        ticks(game, 200);
        const auto events = game.takeEvents();
        require(std::none_of(events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<ConstructionFinished>(event);
        }), "Cancelled or destroyed summoning completed later");
    });
    test("Building cancellation refunds half the crystals and releases all reserved supply", [&] {
        const auto definitions = rts::Definitions::load(assets / "data/catalog.json");
        auto types = definitions.entities();
        auto annex = definitions.entity("church.hall"); annex.id = "test.annex";
        annex.width = annex.height = 1; annex.cost = {75, 99}; types.push_back(annex);
        auto s = flatScenario(); s.slots[0].startingCrystals = 500;
        rts::Simulation game(std::move(s), {}, definitions.entity("church.worker"), std::move(types), definitions.startingDepot(definitions.entity("church.worker").factionId).id);
        const std::array<rts::EntityId, 1> ids{game.worker().id};
        const auto extra = game.construct(ids, annex.id, {3, 5});
        require(extra && game.armySupply().used() == 100 && game.storedCrystals() == 425, "Building did not reserve its common cost");
        require(!game.construct(ids, annex.id, {4, 5}), "Construction exceeded supply cap");
        require(game.map().walkable({4, 5}) && game.storedCrystals() == 425 && game.buildings().size() == 2, "Rejected construction changed world or resources");
        for (int i = 0; i < 300 && game.building(*extra)->constructionProgress < game.building(*extra)->definition.constructionTicks / 2; ++i) game.tick();
        require(game.building(*extra)->constructionProgress > 0 && !game.building(*extra)->complete(), "Partial construction fixture did not advance");
        game.command(ids, {4, 8});
        require(game.cancelConstruction(*extra), "Could not cancel supply-consuming construction");
        require(game.worker().currentOrder.kind == OrderKind::Move, "Cancelling summoning stopped the freed worker's new order");
        require(game.map().walkable({3, 5}) && game.armySupply().used() == 1 && game.storedCrystals() == 462, "Cancellation did not release footprint/supply and refund half the crystals");
        require(!game.cancelConstruction(*extra) && game.storedCrystals() == 462, "Cancellation refunded twice");
    });
    test("Production skips cliffs, ramps, obstacles and incompatible water in perimeter order", [] {
        auto s = flatScenario(); s.slots[0].startingCrystals = 100;
        s.map.at({1, 3}).height = -1;
        s.map.at({2, 3}).ramp = {0, 1};
        s.map.at({3, 3}).height = 1;
        s.map.at({4, 2}).blocked = true;
        s.map.at({4, 1}).surface = rts::Surface::DeepWater;
        rts::EntityDefinition worker; worker.trainingTicks = 1;
        auto game = makeTestSimulation(std::move(s), {}, worker);
        const auto hall = game.buildings().front().id;
        require(game.building(hall)->rally == rts::Cell{3, 0}, "Default rally selected forbidden terrain or skipped perimeter order");
        require(game.train(hall), "Could not train near terrain obstacles");
        game.tick();
        require(game.units().size() == 2 && game.units().back().cell == rts::Cell{3, 0}, "Production selected forbidden terrain or skipped perimeter order");
    });
    test("Production reserves supply, refunds cancellation and follows per-building rally", [&] {
        auto definitions = rts::Definitions::load(assets / "data/catalog.json");

        auto s = flatScenario(); s.slots[0].startingCrystals = 500;
        rts::Simulation game(std::move(s), {}, definitions.entity("church.worker"), definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id);
        const auto hall = game.buildings()[0].id;
        const std::array<rts::EntityId, 1> ids{game.worker().id};
        const auto barracks = game.construct(ids, "church.barracks", {5, 3});
        require(barracks.has_value(), "No barracks"); ticks(game, 300);
        require(game.setRally(hall, {3, 7}) && game.setRally(*barracks, {8, 5}), "Rally rejected");
        require(game.building(hall)->rally != game.building(*barracks)->rally, "Rally not independent");
        require(game.train(*barracks) && game.train(*barracks), "Cannot queue two soldiers");
        require(game.building(*barracks)->training(), "Queued production did not activate training effects");
        require(game.armySupply().used() == 5 && game.storedCrystals() == 280, "Queue did not reserve cost and supply");
        require(game.cancelTraining(*barracks), "Queue cancellation failed");
        require(game.building(*barracks)->training(), "Cancelling one of two jobs stopped training effects");
        require(game.armySupply().used() == 3 && game.storedCrystals() == 310, "Queue refund incorrect");
        ticks(game, 600);
        require(game.units().size() == 2 && game.armySupply().used() == 3, "Spawn charged supply twice");
        require(!game.building(*barracks)->training(), "Finished production left training effects active");
        require(game.train(*barracks) && game.cancelTraining(*barracks) && !game.building(*barracks)->training(), "Cancelled last job left training effects active");
        require(game.units().back().definitionId == "church.peacemaker" && game.units().back().cell == rts::Cell{8, 5}, "Produced unit missed rally");
        require(!game.setRally(*barracks, {7, 7}), "Rally accepted blocked crystal");
    });
    test("Completed production waits for a blocked exit and can still be cancelled", [] {
        auto s = flatScenario(); s.slots[0].startingCrystals = 300; s.worker = {1, 0};
        for (rts::Cell c : rts::perimeter(s.map, s.hall, 3, 2)) if (c != s.worker) s.map.at(c).blocked = true;
        auto game = makeTestSimulation(std::move(s));
        const auto hall = game.buildings()[0].id;
        require(game.train(hall), "Could not train");
        ticks(game, 150);
        require(game.units().size() == 1 && game.building(hall)->production.front().remainingTicks == 0, "Spawn overlapped blocked exit");
        require(!game.building(hall)->training(), "Finished job blocked at exit still counted as training");
        require(game.cancelTraining(hall) && game.armySupply().used() == 1 && game.storedCrystals() == 280, "Blocked job did not refund half");
        require(game.train(hall), "Could not retrain");
        game.command({0, 0}); ticks(game, 150);
        require(game.units().size() == 2 && game.armySupply().used() == 2, "Production did not resume after exit cleared");
    });
    test("Queue limit and hard supply cap reject without consuming crystals", [] {
        auto s = flatScenario(); s.slots[0].startingCrystals = 1000;
        rts::EntityDefinition type; type.cost.supply = 20;
        auto game = makeTestSimulation(std::move(s), {}, type);
        const auto hall = game.buildings()[0].id;
        for (int i = 0; i < 4; ++i) require(game.train(hall), "Could not reserve full army");
        const auto balance = game.storedCrystals();
        require(!game.train(hall) && game.armySupply().used() == 100 && game.storedCrystals() == balance, "Army exceeded 100 or failed order charged");
        require(game.cancelTraining(hall) && game.train(hall), "Released supply not reusable");
        auto normal = flatScenario(); normal.slots[0].startingCrystals = 1000;
        auto queued = makeTestSimulation(std::move(normal));
        const auto depot = queued.buildings()[0].id;
        const auto initialBalance = queued.storedCrystals(); const int initialSupply = queued.armySupply().used();
        for (int i = 0; i < 10; ++i) require(queued.train(depot), "Queue rejected available slot");
        const auto fullBalance = queued.storedCrystals(); const int fullSupply = queued.armySupply().used();
        require(!queued.train(depot) && queued.building(depot)->production.size() == 10, "Queue exceeded ten slots");
        require(queued.storedCrystals() == fullBalance && queued.armySupply().used() == fullSupply, "Full queue rejection consumed resources");
        require(queued.cancelTraining(depot) && queued.train(depot), "Tenth queue slot could not be reused");
        for (int i = 0; i < 10; ++i) require(queued.cancelTraining(depot), "Could not cancel the full queue");
        require(queued.building(depot)->production.empty() && queued.storedCrystals() == initialBalance - 11 * 20 &&
            queued.armySupply().used() == initialSupply, "Full queue cancellation did not refund resources");
    });
    test("Indexed cancellation removes only the chosen job, refunds half and preserves other progress", [] {
        auto scene = flatScenario(); scene.slots[0].startingCrystals = 1000;
        EntityDefinition worker; worker.cost = {75, 1};
        auto second = worker; second.id = "test.second"; second.cost = {40, 2};
        auto third = worker; third.id = "test.third"; third.cost = {60, 3};
        auto game = makeTestSimulation(std::move(scene), {}, worker, {worker, second, third});
        auto& building = const_cast<Building&>(game.buildings().front());
        building.definition.trainableUnits = {worker.id, second.id, third.id};
        for (const auto& id : {worker.id, second.id, third.id, worker.id}) require(game.train(building.id, id), "Could not fill mixed queue");
        require(game.storedCrystals() == 750 && game.armySupply().used() == 8, "Orders did not charge up front");
        ticks(game, 10);
        const int progress = building.production.front().remainingTicks;
        require(!game.cancelTraining(building.id, 4) && game.storedCrystals() == 750 && building.production.size() == 4,
            "Out-of-range cancellation changed queue or balance");
        building.owner = 1;
        require(!game.cancelTraining(building.id, 1) && game.storedCrystals() == 750, "Foreign queue could be cancelled");
        building.owner = game.player().id;
        require(game.cancelTraining(building.id, 1) && building.production.size() == 3 && game.storedCrystals() == 770 &&
            game.armySupply().used() == 6, "Middle cancellation refunded the wrong order");
        require(building.production.front().remainingTicks == progress && building.production[1].definitionId == third.id &&
            building.production[2].definitionId == worker.id, "Middle cancellation changed progress or queue order");
        require(game.cancelTraining(building.id, 0) && game.storedCrystals() == 807 && game.armySupply().used() == 5 &&
            building.production.front().remainingTicks == third.trainingTicks, "Active cancellation transferred progress or rounded refund up");
        game.tick();
        require(building.production.front().remainingTicks == third.trainingTicks - 1, "Next job did not start after cancelling the active job");
        require(game.cancelTraining(building.id, 1) && game.storedCrystals() == 844 && building.production.front().definitionId == third.id,
            "Last-slot cancellation removed the active job");
        require(game.cancelTraining(building.id) && building.production.empty() && game.storedCrystals() == 874 && game.armySupply().used() == 1,
            "Default cancellation failed to release the last job");
        require(!game.cancelTraining(building.id, 0) && game.storedCrystals() == 874, "Empty queue refunded twice");
    });
}
}
