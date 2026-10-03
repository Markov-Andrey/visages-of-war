#include "TestSupport.hpp"

namespace rts::tests {
void combatTests(TestSuite& test, const TestContext& context) {
    test("Damage feedback holds, drains, combines hits and clears immediately on a lethal blow", [] {
        rts::HealthFeedback feedback;
        constexpr std::uint64_t first = 100;
        feedback.record(150, 110, first);
        require(feedback.remaining(first) == 40 && feedback.remaining(first + rts::HealthFeedback::holdTicks) == 40,
            "Damage feedback did not hold the full hit");
        const auto middle = first + rts::HealthFeedback::holdTicks + rts::HealthFeedback::drainTicks / 2;
        const float drained = feedback.remaining(middle);
        require(drained > 0 && drained < 40, "Red trail did not decay after its hold");
        feedback.record(110, 80, middle);
        feedback.record(80, 70, middle);
        require(std::abs(feedback.remaining(middle) - (drained + 40)) < .001f, "Burst lost earlier damage or revived an already drained portion");
        float previous = feedback.remaining(middle);
        for (int dt = 1; dt <= rts::HealthFeedback::holdTicks + rts::HealthFeedback::drainTicks; ++dt) {
            const float current = feedback.remaining(middle + dt);
            require(current >= 0 && current <= previous, "Damage trail grew or overshot while draining"); previous = current;
        }
        require(previous == 0, "Damage trail never finished");
        feedback.record(70, 70, middle + 100);
        require(feedback.remaining(middle + 100) == 0, "Zero damage created a trail");
        feedback.record(70, 30, middle + 100);
        feedback.record(30, 0, middle + 101);
        require(feedback.remaining(middle + 101) == 0 && feedback.remaining(middle + 102) == 0, "Fatal damage retained a visual trail");
    });
    test("Combat applies real damage immediately and health feedback survives level gains without changing combat", [] {
        auto site = flatScenario(); site.extraWorkers = {{4, 4}};
        rts::EntityDefinition attacker; attacker.attackDamage = 20; attacker.attackWindupTicks = 1;
        attacker.attackRecoveryTicks = 0; attacker.attackCooldownTicks = 1000;
        auto victim = attacker; victim.id = "health.victim"; victim.maximumHealth = 150; victim.attackDamage = 0; victim.hero.emplace();
        site.units = {{victim.id, 1, {5, 3}}};
        rts::Simulation game(std::move(site), {}, attacker, {attacker, victim});
        const auto id = game.units().back().id;
        ticks(game, 2);
        require(game.unit(id)->health == 110 && game.unit(id)->healthFeedback.remaining(game.clock().elapsedTicks()) == 40,
            "Simultaneous hits delayed real damage or lost their combined visual amount");
        game.grantExperience(id, 100);
        require(game.unit(id)->health == 150 && game.unit(id)->maximumHealth() == 190 &&
            game.unit(id)->healthFeedback.remaining(game.clock().elapsedTicks()) == 40, "Level health gain became fake damage or erased recent hits");
        ticks(game, rts::HealthFeedback::holdTicks + rts::HealthFeedback::drainTicks);
        require(game.unit(id)->health == 150 && game.unit(id)->healthFeedback.remaining(game.clock().elapsedTicks()) == 0,
            "Trail affected real health or waited for a render to expire");
    });
    test("Attack move fights en route and resumes its destination while move ignores enemies", [] {
        for (const auto kind : {rts::OrderKind::Move, rts::OrderKind::AttackMove}) {
            rts::Scenario site{rts::Map(32, 18), {1, 1}, {4, 8}, {}};
            rts::EntityDefinition fighter; fighter.canBuild = false; fighter.carryCapacity = 0; fighter.attackDamage = 30;
            fighter.movementPerSecond = 4; fighter.dayVision = fighter.nightVision = 5;
            auto enemy = fighter; enemy.id = "enemy"; enemy.attackDamage = 0; enemy.maximumHealth = 30;
            site.units = {{enemy.id, 1, {10, 8}}};
            rts::Simulation game(std::move(site), {}, fighter, {fighter, enemy});
            const auto target = game.units().back().id;
            require(game.order(std::array{game.worker().id}, kind, {23, 8}), "Travel order rejected");
            ticks(game, 600);
            require(game.worker().cell == rts::Cell{23, 8} && rts::unitOrder(game.worker()) == rts::OrderKind::Stop, "Travel destination was lost after combat");
            require((game.unit(target) != nullptr) == (kind == rts::OrderKind::Move), "Move and attack move used the same acquisition behavior");
        }
    });
    test("Hold position attacks only within range and never chases", [] {
        for (const rts::Cell target : {rts::Cell{5, 8}, rts::Cell{9, 8}}) {
            rts::Scenario site{rts::Map(24, 18), {1, 1}, {4, 8}, {}};
            rts::EntityDefinition fighter; fighter.attackDamage = 30; fighter.canBuild = false; fighter.carryCapacity = 0;
            auto enemy = fighter; enemy.id = "enemy"; enemy.attackDamage = 0; enemy.maximumHealth = 30;
            site.units = {{enemy.id, 1, target}};
            rts::Simulation game(std::move(site), {}, fighter, {fighter, enemy});
            const auto victim = game.units().back().id;
            require(game.order(std::array{game.worker().id}, rts::OrderKind::Hold), "Hold rejected");
            ticks(game, 180);
            require(game.worker().cell == rts::Cell{4, 8} && game.worker().position == rts::center({4, 8}) &&
                rts::unitOrder(game.worker()) == rts::OrderKind::Hold, "Hold chased a target or lost its stance");
            require((game.unit(victim) != nullptr) == (target.x == 9), "Hold did not respect attack reach");
        }
    });
    test("Patrol returns to both endpoints after combat and stop cancels the route", [] {
        rts::Scenario site{rts::Map(32, 18), {1, 1}, {4, 8}, {}};
        rts::EntityDefinition fighter; fighter.attackDamage = 30; fighter.movementPerSecond = 4;
        fighter.dayVision = fighter.nightVision = 4; fighter.canBuild = false; fighter.carryCapacity = 0;
        auto enemy = fighter; enemy.id = "enemy"; enemy.attackDamage = 0; enemy.maximumHealth = 30;
        site.units = {{enemy.id, 1, {10, 8}}};
        rts::Simulation game(std::move(site), {}, fighter, {fighter, enemy});
        const auto id = game.worker().id, victim = game.units().back().id;
        require(game.order(std::array{id}, rts::OrderKind::Patrol, {20, 8}), "Patrol rejected");
        int turns = 0; bool outward = true;
        for (int tick = 0; tick < 900; ++tick) {
            game.tick();
            if (game.worker().cell == (outward ? rts::Cell{20, 8} : rts::Cell{4, 8})) { ++turns; outward = !outward; }
        }
        require(!game.unit(victim) && turns >= 4 && rts::unitOrder(game.worker()) == rts::OrderKind::Patrol, "Combat ended patrol or patrol did not return");
        game.stop(std::array{id}); ticks(game, 15);
        const auto stopped = game.worker().position;
        ticks(game, 150);
        require(game.worker().position == stopped && rts::unitOrder(game.worker()) == rts::OrderKind::Stop, "Patrol resumed after stop");
        require(!game.order(std::array{id}, rts::OrderKind::Patrol, game.worker().cell), "Zero-length patrol was accepted");
    });
    test("Hold issued during movement finishes the reserved step without teleporting", [] {
        rts::Simulation game(flatScenario()); const auto id = game.worker().id;
        require(game.order(std::array{id}, rts::OrderKind::Move, {8, 3}), "Move rejected");
        game.tick(); const auto position = game.worker().position;
        require(game.order(std::array{id}, rts::OrderKind::Hold), "Moving hold rejected");
        require(game.worker().position == position && game.worker().pendingOrder.has_value(), "Hold teleported a moving unit");
        ticks(game, 30);
        require(game.worker().cell == rts::Cell{5, 3} && rts::unitOrder(game.worker()) == rts::OrderKind::Hold, "Hold lost the reserved step");
    });
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
        require(game.corpses().front().remainingTicks == 60 * rts::Simulation::ticksPerSecond, "Default corpse lifetime is not 60 seconds");
        ticks(game, 60 * rts::Simulation::ticksPerSecond - 1);
        require(game.corpses().size() == 2 && game.corpses().front().remainingTicks == 1, "Corpse disappeared before 60 seconds");
        game.tick();
        require(game.corpses().front().phase == rts::CorpsePhase::Sinking && game.bones().empty(), "Body did not begin sinking after sixty seconds");
        ticks(game, rts::Corpse::sinkTicks);
        require(game.corpses().empty() && game.bones().size() == 2, "Bodies did not become bones");
        const auto pile = game.bones().front();
        require(pile.owner == rts::neutralPlayer && pile.remainingTicks == rts::Bones::lifetimeTicks && game.bones(pile.id),
            "Bones did not receive neutral ownership, a fresh lifetime or an ID");
        const auto transitions = game.takeEvents();
        require(std::count_if(transitions.begin(), transitions.end(), [](const auto& e) { return std::holds_alternative<rts::BonesCreated>(e); }) == 2,
            "Bones creation events missing");
        ticks(game, rts::Bones::lifetimeTicks - 1);
        require(game.bones(pile.id)->remainingTicks == 1 && !game.bones(pile.id)->sinking && game.bones(pile.id)->variation == pile.variation,
            "Bones changed art, ID or expired before five minutes");
        game.tick();
        require(game.bones(pile.id)->sinking && game.bones(pile.id)->remainingTicks == rts::Bones::sinkTicks, "Bones skipped their sinking phase");
        ticks(game, rts::Bones::sinkTicks);
        require(game.bones().empty() && !game.bones(pile.id), "Bones were not finally removed");
    });
    test("Decomposing ground and flying casualties retain ownership, then leave neutral passable bones", [&] {
        const auto definitions = rts::Definitions::load(context.assets / "data/catalog.json");
        for (const auto& original : definitions.entities()) if (original.mobile && original.canDecompose) {
            auto fighter = original; fighter.maximumHealth = 10; fighter.attackDamage = 10;
            fighter.attackWindupTicks = 1; fighter.attackRange = 1.5f; fighter.projectile.reset();
            auto victim = fighter; victim.id = "test.victim"; victim.attackDamage = 0;
            rts::Scenario site{rts::Map(20, 20), {1, 1}, {8, 8}, {}};
            site.units = {{victim.id, 1, {9, 8}}};
            rts::Simulation game(std::move(site), {}, fighter, {fighter, victim});
            const auto enemy = game.units().back().id;
            require(game.attack(std::array{game.worker().id}, enemy), "Death fixture rejected attack");
            ticks(game, 2);
            require(!game.unit(enemy) && game.corpses().size() == 1 && game.corpses().front().ageTicks() == 0,
                "Casualty did not create a fresh corpse");
            const auto body = game.corpses().front();
            require(body.id != enemy && body.sourceUnit == enemy && body.owner == 1 && game.corpse(body.id), "Body lost its source, identity or owner");
            require(game.command(std::array{game.worker().id}, {9, 8}), "Corpse cell cannot receive a movement order");
            ticks(game, 60);
            require(game.worker().cell == rts::Cell{9, 8} && game.corpses().size() == 1,
                "Ground or air corpse obstructed its movement layer");
            ticks(game, game.corpses().front().remainingTicks - 1);
            require(game.corpses().size() == 1 && game.corpses().front().ageTicks() == 60 * rts::Simulation::ticksPerSecond - 1,
                "Corpse expired before the default sixty seconds");
            game.tick();
            require(game.corpse(body.id)->phase == rts::CorpsePhase::Sinking && game.corpse(body.id)->owner == 1, "Sinking body changed ownership");
            ticks(game, rts::Corpse::sinkTicks);
            require(game.corpses().empty() && !game.corpse(body.id) && game.bones().size() == 1, "Body-to-bones transition failed");
            const auto& pile = game.bones().front();
            require(pile.id != body.id && pile.sourceCorpse == body.id && pile.sourceUnit == enemy && pile.owner == rts::neutralPlayer &&
                pile.cell == body.cell && pile.position == body.position && pile.height == body.height, "Bones changed position or retained player ownership");
            require(game.command(std::array{game.worker().id}, {8, 8}), "Could not leave bones cell"); ticks(game, 60);
            require(game.command(std::array{game.worker().id}, {9, 8}), "Bones blocked a movement order"); ticks(game, 60);
            require(game.worker().cell == rts::Cell{9, 8} && !game.attack(std::array{game.worker().id}, pile.id), "Bones blocked movement or became an attack target");
        }
    });
    test("Non-decomposing units fade away without persistent bodies or bones", [&] {
        const auto definitions = rts::Definitions::load(context.assets / "data/catalog.json");
        for (const auto* id : {"human.hero", "human.peacemaker"}) {
            auto killer = definitions.entity("human.peacemaker"); killer.attackDamage = 10000; killer.attackWindupTicks = 1;
            auto victim = definitions.entity(id); victim.id = "vanishing.victim"; victim.canDecompose = false; victim.attackDamage = 0;
            rts::Scenario site{rts::Map(20, 20), {1, 1}, {8, 8}, {}}; site.units = {{victim.id, 1, {9, 8}}};
            rts::Simulation game(std::move(site), {}, killer, {killer, victim});
            require(game.attack(std::array{game.worker().id}, game.units().back().id), "Vanish fixture rejected attack"); ticks(game, 2);
            require(game.corpses().size() == 1 && game.corpses().front().phase == rts::CorpsePhase::Vanishing, "Unit left a persistent body");
            const int duration = game.corpses().front().remainingTicks;
            ticks(game, duration - 1); require(game.corpses().size() == 1, "Disappearance animation ended too early");
            game.tick(); require(game.corpses().empty() && game.bones().empty(), "Non-decomposing unit left remains");
            ticks(game, rts::Corpse::lifetimeTicks + rts::Corpse::sinkTicks);
            require(game.bones().empty(), "A vanished unit created delayed bones");
            const auto events = game.takeEvents();
            require(std::none_of(events.begin(), events.end(), [](const auto& e) {
                return std::holds_alternative<rts::CorpseCreated>(e) || std::holds_alternative<rts::BonesCreated>(e);
            }), "Non-decomposing unit exposed interactable remains");
        }
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
        require(game.units().size() == 30 && game.hero() && game.hero()->cell == rts::Cell{17, 20}, "Hero spawn ignored");
        require(game.armySupply().used() == 49 && game.hero()->level() == 1, "Starting hero supply or level incorrect");
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
        require(game.corpses().size() == 1 && game.corpses().front().cell == game.worker().cell,
            "Movement test did not cross a still-existing corpse");
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
