#include "TestSupport.hpp"

namespace rts::tests {
namespace {
void checkCombatMotion(const Simulation& game) {
    for (size_t i = 0; i < game.units().size(); ++i) {
        const auto& a = game.units()[i];
        require(game.map().canTraverse(a.tickPosition, a.position, a.definition.collisionRadius, a.definition.movement), "Combat crossed terrain");
        require(groundLengthSquared(a.position - a.tickPosition) <= std::pow(a.definition.movementPerSecond / Simulation::ticksPerSecond + .00001f, 2), "Combat teleported a unit");
        for (size_t j = i + 1; j < game.units().size(); ++j) {
            const auto& b = game.units()[j];
            if (airborne(a.definition.movement) == airborne(b.definition.movement))
                require(!sweptCircleIntersects(a.tickPosition - b.tickPosition, a.position - b.position,
                    a.definition.collisionRadius, {{}, b.definition.collisionRadius}), "Combat bodies crossed during a tick");
        }
    }
}
}
void combatTests(TestSuite& test, const TestContext& context) {
    test("Stage two footprint towers acquire targets and obey forced attack and stop", [] {
        auto worker = fixtureWorker(); worker.maximumHealth = 1000;
        auto tower = fixtureDepot(worker); tower.attackDamage = 10; tower.attackRange = 6;
        tower.attackWindupTicks = 0; tower.attackRecoveryTicks = 0; tower.attackCooldownTicks = 5;
        ProjectileDefinition shot; shot.speed = 20; tower.projectile = shot;
        auto scene = flatScenario(0); scene.slots[0].startingCrystals = 1000; scene.units = {{worker.id, 1, {6, 5}}};
        auto game = makeTestSimulation(scene, {}, worker, {tower}); game.revealMap();
        const auto towerId = game.buildings()[0].id, enemy = game.units()[1].id, own = game.worker().id;
        const auto position = game.entity(towerId)->position;
        ticks(game, 60);
        require(game.entity(enemy)->health < worker.maximumHealth && game.entity(towerId)->position == position, "Tower did not fire or moved");
        require(game.attack(std::array{towerId, own}, own, true), "Tower forced attack rejected");
        ticks(game, 60); require(game.entity(own)->health < worker.maximumHealth, "Tower forced attack did not execute");
        game.stop(std::array{towerId}); require(game.entity(towerId)->currentOrder.kind == OrderKind::Stop, "Tower stop failed");
    });
    test("Forced friendly attacks filter the whole selection by weapon capabilities and reject self", [] {
        EntityDefinition fighter; fighter.id = "fighter"; fighter.attackDamage = 10; fighter.maximumHealth = 1000;
        fighter.attackWindupTicks = 0; fighter.attackRecoveryTicks = 0; fighter.attackCooldownTicks = 30;
        fighter.dayVision = fighter.nightVision = 30; fighter.hero = HeroDefinition{};
        auto worker = fixtureWorker(); worker.id = "worker";
        auto flyer = fighter; flyer.id = "flyer"; flyer.movement = MovementType::Flying; flyer.hero.reset();
        auto flexible = flyer; flexible.id = "flexible"; flexible.attackTargets = AttackTargets::Ground;
        Scenario scene{Map(24, 20), {1,1}, {7,8}, {}};
        scene.units = {{fighter.id,0,{8,8}}, {worker.id,0,{6,8}}, {flyer.id,0,{7,9}}, {flexible.id,0,{8,9}}};
        auto game = makeTestSimulation(std::move(scene), {}, fighter, {fighter, worker, flyer, flexible});
        std::vector<EntityId> ids; for (const auto& u : game.units()) ids.push_back(u.id);
        const auto target = ids[1];
        game.order(ids, OrderKind::Hold); ticks(game, 3);
        require(game.unit(target)->health == 1000 && !game.attack(ids, target), "Automatic or ordinary attack harmed a friend");
        require(!game.attack(std::array{target}, target, true), "Unit accepted an attack against itself");
        require(game.attack(ids, target, true), "Forced friendly attack rejected");
        require(game.unit(ids[0])->attackTarget == target && game.unit(ids[4])->attackTarget == target,
            "Capable members did not receive attack or flight was hardcoded as incapable");
        for (size_t i : {size_t{1}, size_t{2}, size_t{3}})
            require(!game.unit(ids[i])->attackTarget && game.unit(ids[i])->currentOrder.kind == OrderKind::Hold,
                "Self, zero damage worker or incompatible layer lost its old order");
        ticks(game, 2);
        require(game.unit(target)->health == 980, "Explicit allied melee hits were filtered out");
        game.stop(ids); ticks(game, 40);
        require(game.unit(target)->health == 980, "Stop caused automatic friendly retaliation");
        auto& victim = const_cast<Unit&>(*game.unit(target)); victim.health = 1;
        require(game.attack(std::array{ids[0]}, target, true), "Repeated forced attack rejected");
        ticks(game, 2);
        require(!game.unit(target) && game.unit(ids[0])->hero->experience == 0 && !game.unit(ids[0])->attackTarget,
            "Friendly kill awarded XP or retained a destroyed target");
    });
    test("Forced melee attacks approach square buildings and never strike through cliffs", [] {
        for (Cell start : {Cell{12,8}, Cell{3,12}, Cell{2,3}}) {
            EntityDefinition fighter; fighter.attackDamage = 10; fighter.dayVision = fighter.nightVision = 30;
            fighter.attackWindupTicks = 0; fighter.attackRecoveryTicks = 1; fighter.attackCooldownTicks = 1;
            Scenario scene{Map(24, 20), {4,4}, start, {}};
            auto game = makeTestSimulation(std::move(scene), {}, fighter);
            const auto hall = game.buildings().front().id, attacker = game.worker().id;
            require(!game.attack(std::array{attacker}, hall) && game.attack(std::array{attacker}, hall, true), "Building ownership filter is wrong");
            for (int tick = 0; tick < 240 && game.building(hall)->health == 1000; ++tick) { game.tick(); checkCombatMotion(game); }
            require(game.building(hall)->health < 1000, "Melee never approached a building perimeter");
            const_cast<Building&>(*game.building(hall)).health = 1;
            ticks(game, 5);
            require(!game.building(hall) && !game.worker().attackTarget && game.worker().currentOrder.kind == OrderKind::Stop,
                "Building death left an attack or occupied footprint");
            require(game.map().walkable({4,4}), "Destroyed building kept its collision");
        }
        EntityDefinition fighter; fighter.attackDamage = 10; fighter.attackRange = 3;
        fighter.dayVision = fighter.nightVision = 30;
        Scenario cliff{Map(20,20), {4,4}, {7,5}, {}};
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 7; ++x) cliff.map.at({x,y}).height = 1;
        auto game = makeTestSimulation(std::move(cliff), {}, fighter);
        const auto hall = game.buildings().front().id;
        require(game.attack(std::array{game.worker().id}, hall, true), "Visible cliff target order rejected");
        ticks(game, 90);
        require(game.building(hall)->health == 1000, "Melee struck a building through a cliff");
    });
    test("Melee reaches continuous attack positions when no free cell centre is in range", [] {
        for (float radius : {.1f, .35f, .5f}) for (float reach : {0.0f, .15f}) {
            Scenario site{Map(24, 20), {1, 1}, {4, 8}, {}};
            EntityDefinition fighter; fighter.collisionRadius = radius; fighter.attackRange = 2 * radius + reach;
            fighter.attackDamage = 10; fighter.dayVision = fighter.nightVision = 20;
            auto victim = fighter; victim.id = "victim"; victim.attackDamage = 0; victim.maximumHealth = 1000;
            site.units = {{victim.id, 1, {8, 8}}};
            auto game = makeTestSimulation(std::move(site), {}, fighter, {fighter, victim});
            const auto enemy = game.units().back().id;
            require(game.attack(std::array{game.worker().id}, enemy), "Continuous attack rejected");
            for (int tick = 0; tick < 180; ++tick) { game.tick(); checkCombatMotion(game); }
            require(game.unit(enemy)->health < 1000, "Melee waited for a cell-centred attack slot");
            if (radius < .5f || reach > 0)
                require(groundLengthSquared(game.worker().position - center(game.worker().cell)) > .001f, "Attack still snapped to a cell centre");
        }
    });
    test("Focused attacker preserves its detour around a stationary friendly screen", [] {
        Scenario site{Map(28, 24), {1, 1}, {6, 10}, {}};
        EntityDefinition fighter; fighter.attackDamage = 10; fighter.attackRange = 1.1f;
        fighter.collisionRadius = .45f; fighter.dayVision = fighter.nightVision = 20;
        auto passive = fighter; passive.id = "passive"; passive.attackDamage = 0; passive.maximumHealth = 1000;
        site.units = {{passive.id, 0, {9, 9}}, {passive.id, 0, {9, 10}}, {passive.id, 0, {9, 11}}, {passive.id, 1, {12, 10}}};
        auto game = makeTestSimulation(std::move(site), {}, fighter, {fighter, passive});
        const auto enemy = game.units().back().id;
        require(game.attack(std::array{game.worker().id}, enemy), "Screen attack rejected");
        for (int tick = 0; tick < 360; ++tick) {
            game.tick(); checkCombatMotion(game);
            require(game.worker().attackTarget == enemy && game.worker().currentOrder.kind == OrderKind::Attack, "Explicit target was replaced");
        }
        require(game.unit(enemy)->health < 1000, "Attacker failed to go around a static screen");
        for (size_t i = 1; i < game.units().size(); ++i)
            require(game.units()[i].position == center(game.units()[i].cell), "Combat pushed a stationary body");
    });
    test("Automatic combat abandons an unreachable nearest target for an accessible enemy", [] {
        Scenario site{Map(24, 20), {1, 1}, {6, 10}, {}};
        for (int y = 0; y < 20; ++y) site.map.at({7, y}).blocked = true;
        EntityDefinition fighter; fighter.attackDamage = 10; fighter.attackRange = 1.1f;
        fighter.dayVision = fighter.nightVision = 20;
        auto victim = fighter; victim.id = "victim"; victim.attackDamage = 0; victim.maximumHealth = 1000;
        site.units = {{victim.id, 1, {8, 10}}, {victim.id, 1, {4, 14}}};
        auto game = makeTestSimulation(std::move(site), {}, fighter, {fighter, victim});
        for (int tick = 0; tick < 240; ++tick) { game.tick(); checkCombatMotion(game); }
        require(game.units()[1].health == 1000 && game.units()[2].health < 1000, "Unreachable target pinned automatic combat");
    });
    test("Dense opposing squads keep fighting through casualties without losing focused orders", [] {
        for (bool focused : {false, true}) {
            Scenario site{Map(48, 40), {1, 1}, {14, 16}, {}};
            EntityDefinition fighter; fighter.attackDamage = 12; fighter.maximumHealth = 1000;
            fighter.collisionRadius = .45f; fighter.dayVision = fighter.nightVision = 20;
            auto enemy = fighter; enemy.id = "enemy"; enemy.attackDamage = 1; enemy.maximumHealth = 36;
            for (int i = 0; i < 24; ++i) {
                if (i) site.units.push_back({fighter.id, 0, {14 + i % 4, 16 + i / 4}});
                site.units.push_back({enemy.id, 1, {24 + i % 4, 16 + i / 4}});
            }
            auto game = makeTestSimulation(std::move(site), {}, fighter, {fighter, enemy});
            std::vector<EntityId> own;
            EntityId target{};
            for (const auto& u : game.units()) { if (u.owner == 0) own.push_back(u.id); else if (!target) target = u.id; }
            if (focused) require(game.attack(own, target), "Dense focus rejected");
            for (int tick = 0; tick < 900; ++tick) { game.tick(); checkCombatMotion(game); }
            if (game.units().size() != 24) for (const auto& u : game.units())
                std::cerr << "battle " << focused << " unit " << u.id << " owner=" << u.owner << " hp=" << u.health
                          << " pos=" << u.position.x << ',' << u.position.y << " blocked=" << u.blockedTicks << " target=" << u.attackTarget << '\n';
            require(game.units().size() == 24 && std::all_of(game.units().begin(), game.units().end(), [](const Unit& u) { return u.owner == 0; }),
                "Dense squad stopped attacking surviving enemies");
        }
    });
    test("Untargeted abilities spend mana once, preserve orders and unlock after exactly five simulation seconds", [&] {
        const auto definitions = Definitions::load(context.assets / "data/catalog.json");
        Scenario site{Map(32, 32), {1, 1}, {4, 6}, {}};
        site.heroSpawn = Cell{6, 6}; site.units = {{"church.hero", 1, {28, 28}}};
        Simulation game(std::move(site), {}, definitions.entity("church.worker"), definitions.entities(), definitions.startingDepot(definitions.entity("church.worker").factionId).id, "church.hero");
        const auto id = game.units().back().id, foreign = game.units()[1].id;
        require(game.unit(id)->mana == 200 && game.worker().mana == 0, "Spawn did not initialize independent mana pools");
        require(game.order(std::array{id}, OrderKind::Move, {12, 6}), "Ability movement setup failed");
        const auto position = game.unit(id)->position;
        require(!game.activateAbility(game.worker().id, 1) && !game.activateAbility(foreign, 1) &&
            !game.activateAbility(id, 999) && !game.activateAbility(99999, 1), "Unknown or unauthorized caster accepted");
        require(game.activateAbility(id, 1) && game.unit(id)->mana == 175 && game.unit(id)->abilityCooldown(1) == 150,
            "Ability failed to atomically spend mana and start cooldown");
        require(game.unit(id)->currentOrder.kind == OrderKind::Move && game.unit(id)->position == position,
            "Untargeted ability changed movement or required a target");
        require(!game.activateAbility(id, 1) && game.unit(id)->mana == 175 && game.unit(id)->abilityCooldown(1) == 150,
            "Repeated click spent mana or restarted cooldown");
        ticks(game, 149);
        require(game.unit(id)->abilityCooldown(1) == 1 && !game.activateAbility(id, 1), "Cooldown ended before five seconds");
        game.tick();
        require(game.unit(id)->abilityCooldown(1) == 0 && game.activateAbility(id, 1), "Cooldown did not expire on its final tick");
        while (game.unit(id)->mana > 0) {
            ticks(game, 150); require(game.activateAbility(id, 1), "Ready ability rejected sufficient mana");
        }
        ticks(game, 150);
        require(!game.activateAbility(id, 1) && game.unit(id)->mana == 0 && game.unit(id)->abilityCooldown(1) == 0,
            "Insufficient mana went negative or started cooldown");
        require(game.unit(foreign)->mana == 200 && game.unit(foreign)->abilityCooldown(1) == 0, "Cooldown or mana leaked between units");
        game.grantExperience(id, 100);
        require(game.unit(id)->maximumMana() == 200 && game.unit(id)->mana == 0, "Level up unexpectedly refilled mana");
    });
    test("Attack move fights en route and resumes its destination while move ignores enemies", [] {
        for (const auto kind : {rts::OrderKind::Move, rts::OrderKind::AttackMove}) {
            rts::Scenario site{rts::Map(32, 18), {1, 1}, {4, 8}, {}};
            rts::EntityDefinition fighter; fighter.canBuild = false; fighter.carryCapacity = 0; fighter.attackDamage = 30;
            fighter.movementPerSecond = 4; fighter.dayVision = fighter.nightVision = 5;
            auto enemy = fighter; enemy.id = "enemy"; enemy.attackDamage = 0; enemy.maximumHealth = 30;
            site.units = {{enemy.id, 1, {10, 8}}};
            auto game = makeTestSimulation(std::move(site), {}, fighter, {fighter, enemy});
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
            auto game = makeTestSimulation(std::move(site), {}, fighter, {fighter, enemy});
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
        auto game = makeTestSimulation(std::move(site), {}, fighter, {fighter, enemy});
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
    const auto loadScenario = [&](const std::filesystem::path& file) { return rts::loadScenario(file, context.worldAssets, context.hallFootprint); };
    test("Melee windup, simultaneous deaths, supply, events, corpses and empty army", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier;
        soldier.attackDamage = soldier.maximumHealth; soldier.canBuild = false; soldier.cost.supply = 2;
        scenario.units.push_back({soldier.id, 1, {5, 3}});
        auto game = makeTestSimulation(std::move(scenario), {}, soldier);
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
        require(game.corpses().front().phase == rts::CorpsePhase::Fading && game.bones().size() == 2,
            "Bones did not appear when bodies began fading after sixty seconds");
        ticks(game, rts::Corpse::fadeTicks);
        require(game.corpses().empty() && game.bones().size() == 2, "Bodies did not become bones");
        const auto pile = game.bones().front();
        require(pile.owner == rts::neutralPlayer && pile.remainingTicks == rts::Bones::lifetimeTicks - rts::Corpse::fadeTicks && game.bones(pile.id),
            "Bones lost neutral ownership, their lifetime or an ID");
        const auto transitions = game.takeEvents();
        require(std::count_if(transitions.begin(), transitions.end(), [](const auto& e) { return std::holds_alternative<rts::BonesCreated>(e); }) == 2,
            "Bones creation events missing");
        ticks(game, pile.remainingTicks - 1);
        require(game.bones(pile.id)->remainingTicks == 1 && !game.bones(pile.id)->fading && game.bones(pile.id)->variation == pile.variation,
            "Bones changed art, ID or expired before five minutes");
        game.tick();
        require(game.bones(pile.id)->fading && game.bones(pile.id)->remainingTicks == rts::Bones::fadeTicks, "Bones skipped their fading phase");
        ticks(game, rts::Bones::fadeTicks);
        require(game.bones().empty() && !game.bones(pile.id), "Bones were not finally removed");
    });
    test("Hero progression crosses thresholds, caps at ten and leaves definitions unchanged", [] {
        rts::EntityDefinition hero;
        hero.hero.emplace(); hero.attackDamage = 24; hero.maximumHealth = 400;
        auto game = makeTestSimulation(flatScenario(), {}, hero);
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
        auto next = makeTestSimulation(flatScenario(), {}, hero);
        require(next.hero()->level() == 1 && next.hero()->hero->experience == 0, "Progress leaked between matches");
    });
    test("Kills grant static-level XP to nearby heroes, shared once and never to ordinary units", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier; soldier.id = "soldier"; soldier.canBuild = false; soldier.carryCapacity = 0; soldier.attackDamage = 60;
        auto hero = soldier; hero.id = "hero"; hero.hero.emplace(); hero.attackDamage = 0;
        auto victim = soldier; victim.id = "victim"; victim.attackDamage = 0; victim.level = 3;
        scenario.units = {{victim.id, 1, {5, 3}}, {hero.id, 0, {4, 5}}, {hero.id, 0, {5, 5}}};
        auto game = makeTestSimulation(std::move(scenario), {}, soldier, {soldier, hero, victim});
        const auto enemy = game.units()[1].id, first = game.units()[2].id, second = game.units()[3].id;
        ticks(game, 7);
        require(!game.unit(enemy), "Enemy survived lethal hit");
        require(game.unit(first)->hero->experience == 75 && game.unit(second)->hero->experience == 75, "Level-three bounty not divided equally");
        require(!game.worker().hero && game.worker().level() == 1 && !game.grantExperience(game.worker().id, 100), "Ordinary unit leveled");
        ticks(game, 50);
        require(game.unit(first)->hero->experience == 75, "Corpse granted XP twice");
    });
    test("Hostile orders rejected and explicit move interrupts a swing", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier; soldier.attackDamage = 2; soldier.canBuild = false;
        scenario.units.push_back({soldier.id, 1, {5, 3}});
        auto game = makeTestSimulation(std::move(scenario), {}, soldier);
        const auto friendly = game.units()[0].id, enemy = game.units()[1].id;
        const std::array own{friendly}, hostile{enemy};
        require(!game.command(hostile, {9, 9}) && !game.attack(hostile, friendly), "Player controlled enemy");
        ticks(game, 3);
        const auto target = game.unit(enemy)->attackTarget;
        game.stop(hostile);
        require(target == friendly && game.unit(enemy)->attackTarget == target, "Player stopped enemy");
        require(game.command(own, {3, 8}), "Move rejected");
        require(game.unit(friendly)->attackTarget == 0 && game.unit(friendly)->attackPhase != rts::AttackPhase::Windup, "Move failed to cancel combat");
        ticks(game, 8);
        require(game.unit(enemy)->health == soldier.maximumHealth, "Cancelled swing still damaged enemy");
    });
    test("Melee respects cliffs and excludes ground versus air", [] {
        auto scenario = flatScenario();
        rts::EntityDefinition soldier; soldier.attackDamage = 12; soldier.canBuild = false;
        for (int y = 0; y < 10; ++y) for (int x = 0; x < 5; ++x) scenario.map.at({x, y}).height = 1;
        scenario.units.push_back({soldier.id, 1, {5, 3}});
        auto game = makeTestSimulation(std::move(scenario), {}, soldier);
        const std::array own{game.worker().id};
        require(game.attack(own, game.units()[1].id), "High ground observer should see low target");
        ticks(game, 180);
        for (const auto& u : game.units()) require(u.health == soldier.maximumHealth, "Melee hit through impassable cliff");
        auto airScenario = flatScenario();
        auto flyer = soldier; flyer.id = "test.flyer"; flyer.movement = rts::MovementType::Flying;
        airScenario.units.push_back({flyer.id, 1, {5, 3}});
        auto airGame = makeTestSimulation(std::move(airScenario), {}, soldier, {soldier, flyer});
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
        auto game = makeTestSimulation(std::move(scenario), {}, soldier);
        const std::array own{game.worker().id};
        require(!game.attack(own, game.units()[1].id), "Unseen enemy accepted");
        ticks(game, 60);
        for (const auto& u : game.units()) require(u.attackTarget == 0 && u.state == rts::UnitState::Idle, "Autoattack saw through high terrain");
    });
}
}
