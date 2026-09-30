#include "TestSupport.hpp"

namespace rts::tests {
void combatTests(TestSuite& test, const TestContext& context) {
    const auto& assets=context.assets;
    const auto loadScenario = [&](const std::filesystem::path& file) { return rts::loadScenario(file, context.worldAssets, context.hallFootprint); };
    test("Melee windup, simultaneous deaths, supply, events, corpses and empty army", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier;
        soldier.attackDamage = soldier.maximumHealth; soldier.canBuild = false; soldier.cost.supply = 2;
        scenario.units.push_back({soldier.id, 1, {5, 3}});
        rts::Simulation game(std::move(scenario), {}, soldier);
        const auto friendly = game.units()[0].id, enemy = game.units()[1].id;
        const std::array selection{friendly};
        require(game.attack(selection, enemy), "Visible enemy rejected");
        ticks(game, 6);
        require(game.unit(enemy)->health == soldier.maximumHealth && game.unit(friendly)->health == soldier.maximumHealth, "Damage before impact frame");
        require(rts::unitFrame(*game.unit(friendly)).column >= 4, "Attack animation missing");
        game.tick();
        require(game.units().empty() && game.armySupply().used() == 0, "Simultaneous kill favoured update order or leaked supply");
        const auto events = game.takeEvents();
        require(std::count_if(events.begin(), events.end(), [](const auto& e) { return std::holds_alternative<rts::UnitDied>(e); }) == 2, "Missing death events");
        rts::Selection picked; picked.ids = {friendly, enemy}; picked.prune(game);
        require(picked.ids.empty() && game.corpses().size() == 2, "Dead entities remained selectable or invisible death");
        require(!game.command({4, 4}), "Empty army accepted command"); game.stop();
        ticks(game, 100);
        require(game.corpses().empty(), "Corpses never expire");
    });
    test("Hero progression crosses thresholds, caps at ten and leaves definitions unchanged", [] {
        rts::EntityDefinition hero;
        hero.hero.emplace(); hero.attackDamage = 24; hero.maximumHealth = 400;
        rts::Simulation game(flatScenario(), {}, hero);
        const auto id = game.worker().id;
        require(game.hero() && game.hero()->level() == 1, "Hero not initialized");
        require(!game.grantExperience(id, -1) && !game.grantExperience(id, 0), "Invalid XP accepted");
        game.grantExperience(id, 99);
        require(game.hero()->level() == 1 && game.hero()->experienceInLevel() == 99, "Premature level");
        game.grantExperience(id, 151);
        require(game.hero()->level() == 3 && game.hero()->maximumHealth() == 480 && game.hero()->attackDamage() == 32, "Multiple levels or stats lost");
        require(game.hero()->experienceInLevel() == 0 && game.hero()->experienceToLevel() == 200, "XP bar uses wrong interval");
        require(game.hero()->definition.maximumHealth == 400 && game.hero()->definition.level == 1, "Base data mutated");
        game.grantExperience(id, std::numeric_limits<int>::max());
        require(game.hero()->level() == 10 && game.hero()->hero->experience == 3200 && game.hero()->experienceFraction() == 1, "XP cap overflow");
        require(!game.grantExperience(id, 50), "XP accepted at cap");
        const auto events = game.takeEvents();
        require(std::count_if(events.begin(), events.end(), [](const auto& e) { return std::holds_alternative<rts::HeroLevelChanged>(e); }) == 9, "Missing level events");
        rts::Simulation next(flatScenario(), {}, hero);
        require(next.hero()->level() == 1 && next.hero()->hero->experience == 0, "Progress leaked between matches");
    });
    test("Kills grant static-level XP to nearby heroes, shared once and never to ordinary units", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier; soldier.id = "soldier"; soldier.canBuild = false; soldier.carryCapacity = 0; soldier.attackDamage = 60;
        auto hero = soldier; hero.id = "hero"; hero.hero.emplace(); hero.attackDamage = 0;
        auto victim = soldier; victim.id = "victim"; victim.attackDamage = 0; victim.level = 3;
        scenario.units = {{victim.id, 1, {5, 3}}, {hero.id, 0, {4, 5}}, {hero.id, 0, {5, 5}}};
        rts::Simulation game(std::move(scenario), {}, soldier, {soldier, hero, victim});
        const auto enemy = game.units()[1].id, first = game.units()[2].id, second = game.units()[3].id;
        ticks(game, 7);
        require(!game.unit(enemy), "Enemy survived lethal hit");
        require(game.unit(first)->hero->experience == 75 && game.unit(second)->hero->experience == 75, "Level-three bounty not divided equally");
        require(!game.worker().hero && game.worker().level() == 1 && !game.grantExperience(game.worker().id, 100), "Ordinary unit leveled");
        ticks(game, 50);
        require(game.unit(first)->hero->experience == 75, "Corpse granted XP twice");
    });
    test("Distant and enemy heroes gain no XP and lethal trades cannot revive heroes", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier; soldier.id = "soldier"; soldier.attackDamage = 60;
        auto hero = soldier; hero.id = "hero"; hero.hero.emplace(); hero.attackDamage = 0;
        auto victim = soldier; victim.id = "victim"; victim.attackDamage = 0;
        scenario.units = {{victim.id, 1, {5, 3}}, {hero.id, 0, {4, 6}}, {hero.id, 1, {6, 3}}};
        rts::ProgressionRules rules; rules.experienceRadius = 1;
        rts::Simulation game(std::move(scenario), {}, soldier, {soldier, hero, victim}, {}, rules);
        const auto far = game.units()[2].id, hostileHero = game.units()[3].id;
        ticks(game, 7);
        require(game.unit(far)->hero->experience == 0 && game.unit(hostileHero)->hero->experience == 0, "Wrong owner/radius reward");
        auto duel = flatScenario();
        soldier.hero.emplace(); duel.units = {{soldier.id, 1, {5, 3}}};
        rts::Simulation trade(std::move(duel), {}, soldier);
        const auto id = trade.worker().id;
        trade.grantExperience(id, 99);
        ticks(trade, 7);
        require(!trade.hero() && trade.heroFallen() && trade.units().empty(), "Lethal hero revived by XP");
        require(!trade.grantExperience(id, 100), "Dead hero gained XP");
    });
    test("Army selection includes support and heroes but excludes workers; dead hero selection is inert", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition worker; worker.attackDamage = 10;
        auto support = worker; support.id = "support"; support.canBuild = false; support.carryCapacity = 0; support.attackDamage = 0;
        auto hero = support; hero.id = "hero"; hero.hero.emplace();
        auto killer = support; killer.id = "killer"; killer.attackDamage = 1000;
        scenario.units = {{hero.id, 0, {5, 5}}, {support.id, 0, {8, 8}}, {killer.id, 1, {6, 5}}};
        rts::Simulation game(std::move(scenario), {}, worker, {worker, support, hero, killer});
        rts::Selection selection; selection.army(game);
        const auto heroId = game.hero()->id, supportId = game.units()[2].id;
        require(selection.ids.size() == 2 && selection.contains(heroId) && selection.contains(supportId), "Incorrect army filter");
        require(selection.hero(game) && selection.ids == std::vector<rts::EntityId>{heroId}, "Hero shortcut failed");
        ticks(game, 7); selection.prune(game); selection.army(game);
        require(game.heroFallen() && !game.hero() && !selection.hero(game) && selection.ids == std::vector<rts::EntityId>{supportId}, "Dead hero button changes selection");
    });
    test("Selected commander spawns a fresh hero at the map marker", [&] {
        const auto defs = rts::Definitions::load(assets / "data/catalog.json");
        const auto& commander = defs.commanders().front();
        rts::Simulation game(loadScenario(assets / "maps/demo.rtsmap"), {}, defs.entity(commander.startingWorker), defs.entities(), commander.startingHero);
        require(game.units().size() == 29 && game.hero() && game.hero()->cell == rts::Cell{17, 20}, "Hero spawn ignored");
        require(game.armySupply().used() == 47 && game.hero()->level() == 1, "Starting hero supply or level incorrect");
        mustThrow([&] { rts::Simulation invalid(flatScenario(), {}, defs.entity(commander.startingWorker), defs.entities(), commander.startingHero); });
    });
    test("Hostile orders rejected and explicit move interrupts a swing", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier; soldier.attackDamage = 2; soldier.canBuild = false;
        scenario.units.push_back({soldier.id, 1, {5, 3}});
        rts::Simulation game(std::move(scenario), {}, soldier);
        const auto friendly = game.units()[0].id, enemy = game.units()[1].id;
        const std::array own{friendly}, hostile{enemy};
        require(!game.command(hostile, {9, 9}) && !game.attack(hostile, friendly), "Player controlled enemy");
        ticks(game, 3);
        const auto target = game.unit(enemy)->targetUnit;
        game.stop(hostile);
        require(target == friendly && game.unit(enemy)->targetUnit == target, "Player stopped enemy");
        require(game.command(own, {3, 8}), "Move rejected");
        require(game.unit(friendly)->targetUnit == 0 && game.unit(friendly)->attackPhase != rts::AttackPhase::Windup, "Move failed to cancel combat");
        ticks(game, 8);
        require(game.unit(enemy)->health == soldier.maximumHealth, "Cancelled swing still damaged enemy");
    });
    test("Melee respects cliffs and excludes ground versus air", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier; soldier.attackDamage = 12; soldier.canBuild = false;
        for (int y = 0; y < 10; ++y) for (int x = 0; x < 5; ++x) scenario.map.at({x, y}).height = 1;
        scenario.units.push_back({soldier.id, 1, {5, 3}});
        rts::Simulation game(std::move(scenario), {}, soldier);
        const std::array own{game.worker().id};
        require(game.attack(own, game.units()[1].id), "High ground observer should see low target");
        ticks(game, 180);
        for (const auto& u : game.units()) require(u.health == soldier.maximumHealth, "Melee hit through impassable cliff");
        auto airScenario = flatScenario();
        auto flyer = soldier; flyer.id = "test.flyer"; flyer.movement = rts::MovementType::Flying;
        airScenario.units.push_back({flyer.id, 1, {5, 3}});
        rts::Simulation airGame(std::move(airScenario), {}, soldier, {soldier, flyer});
        const std::array ground{airGame.worker().id};
        require(!airGame.attack(ground, airGame.units()[1].id), "Melee ordered against another movement layer");
        ticks(airGame, 180);
        for (const auto& u : airGame.units()) require(u.health == soldier.maximumHealth, "Melee hit another movement layer");
    });
    test("Units cannot attack unseen enemies or acquire targets through higher terrain", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier; soldier.attackDamage = 12; soldier.canBuild = false;
        for (int y = 0; y < 10; ++y) scenario.map.at({5, y}).height = 1;
        scenario.units.push_back({soldier.id, 1, {6, 3}});
        rts::Simulation game(std::move(scenario), {}, soldier);
        const std::array own{game.worker().id};
        require(!game.attack(own, game.units()[1].id), "Unseen enemy accepted");
        ticks(game, 60);
        for (const auto& u : game.units()) require(u.targetUnit == 0 && u.state == rts::UnitState::Idle, "Autoattack saw through high terrain");
    });
    test("Attack during movement finishes its step and pursuit closes distance", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier; soldier.attackDamage = 12; soldier.canBuild = false;
        auto dummy = soldier; dummy.id = "test.dummy"; dummy.attackDamage = 0;
        scenario.units.push_back({dummy.id, 1, {8, 3}});
        rts::Simulation game(std::move(scenario), {}, soldier, {soldier, dummy});
        const std::array own{game.worker().id};
        const auto enemy = game.units()[1].id;
        require(game.command(own, {4, 8}), "Move rejected"); ticks(game, 2);
        const auto position = game.worker().position;
        require(game.attack(own, enemy), "Attack rejected");
        require(game.worker().position.x == position.x && game.worker().position.y == position.y && game.worker().pendingOrder.has_value(), "Attack teleported mid-step");
        ticks(game, 500);
        require(!game.unit(enemy) && game.units().size() == 1, "Pursuit failed to kill target");
        require(game.armySupply().used() == soldier.cost.supply, "Enemy death changed player supply");
        require(game.worker().targetUnit == 0 && game.worker().state == rts::UnitState::Idle, "Dangling combat order after kill");
        require(game.command(own, {8, 3}), "Dead cell still reserved"); ticks(game, 120);
        require(game.worker().cell == rts::Cell{8, 3}, "Corpse blocks movement");
    });
    test("Squad combat acquires new targets after focused enemy dies", [] {
        rts::Scenario scenario{rts::Map(24, 20), {1, 1}, {8, 8}, {}};
        rts::EntityDefinition soldier; soldier.attackDamage = 12; soldier.canBuild = false;
        auto enemy = soldier; enemy.id = "test.enemy"; enemy.maximumHealth = 24; enemy.attackDamage = 1;
        for (int i = 0; i < 6; ++i) {
            if (i > 0) scenario.units.push_back({soldier.id, 0, {8 + i % 2, 8 + i / 2}});
            scenario.units.push_back({enemy.id, 1, {14 + i % 2, 8 + i / 2}});
        }
        rts::Simulation game(std::move(scenario), {}, soldier, {soldier, enemy});
        std::vector<rts::EntityId> own;
        rts::EntityId target{};
        for (const auto& u : game.units()) { if (u.owner == 0) own.push_back(u.id); else if (!target) target = u.id; }
        require(game.attack(own, target), "Squad attack rejected");
        for (int t = 0; t < 1800; ++t) {
            game.tick();
            for (size_t i = 0; i < game.units().size(); ++i) for (size_t j = i + 1; j < game.units().size(); ++j)
                require(game.units()[i].cell != game.units()[j].cell, "Combat units overlapped");
        }
        require(game.units().size() == 6 && std::all_of(game.units().begin(), game.units().end(), [](const auto& u) { return u.owner == 0; }), "Squad stalled after focused target death");
    });
}
}
