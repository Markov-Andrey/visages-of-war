#include "TestSupport.hpp"
#include <tuple>

namespace rts::tests {
namespace {
EntityDefinition weapon(ProjectileTargeting mode = ProjectileTargeting::Unit) {
    EntityDefinition d; d.id = "shooter"; d.canBuild = false; d.carryCapacity = 0;
    d.maximumHealth = 100; d.attackDamage = 20; d.dayVision = d.nightVision = 20;
    d.attackRange = 8; d.attackWindupTicks = 2; d.attackRecoveryTicks = 3; d.attackCooldownTicks = 300;
    d.attackTargets = AttackTargets::All;
    auto& p = d.projectile.emplace(); p.targeting = mode; p.speed = 6; p.arcHeight = 3;
    if (mode == ProjectileTargeting::Point) { p.splashRadius = 1.4f; d.attackTargets = AttackTargets::Ground; }
    return d;
}
EntityDefinition dummy() {
    auto d = weapon(); d.id = "dummy"; d.attackDamage = 0; d.movementPerSecond = 6; return d;
}
Scenario range() { return {Map(24, 20), {1,1}, {4,6}, {}}; }
Simulation stationary(EntityDefinition gun) {
    auto s = range(); s.units = {{"dummy",1,{10,6}}};
    return makeTestSimulation(std::move(s),{},gun,{gun,dummy()});
}
}
void projectileTests(TestSuite& test, const TestContext&) {
    test("Forced ranged attacks hit allied units and buildings without enabling unrelated friendly splash", [] {
        for (auto mode : {ProjectileTargeting::Unit, ProjectileTargeting::Point}) for (bool buildingTarget : {false, true}) {
            auto gun = weapon(mode); gun.projectile->speed = 100; gun.projectile->friendlyFire = false;
            auto scene = range(); scene.units = {{"dummy",0,{8,6}}, {"dummy",0,{8,7}}};
            auto game = makeTestSimulation(std::move(scene), {}, gun, {gun, dummy()});
            const auto attacker = game.worker().id, friendId = game.units()[1].id, bystander = game.units()[2].id;
            const auto target = buildingTarget ? game.buildings().front().id : friendId;
            require(game.attack(std::array{attacker}, target, true), "Forced ranged order rejected");
            ticks(game, 10);
            require((buildingTarget ? game.building(target)->health : game.unit(target)->health) == (buildingTarget ? 980 : 80),
                "Ranged shot filtered explicit friendly target or damaged it more than once");
            require(game.unit(bystander)->health == 100, "Forced target bypassed splash friendlyFire for unrelated ally");
        }
    });
    test("Forced homing shots retain their allied target after Stop and never retarget a dead victim", [] {
        auto gun = weapon();
        auto scene = range(); scene.units = {{"dummy",0,{10,6}}, {"dummy",0,{10,7}}};
        auto game = makeTestSimulation(std::move(scene), {}, gun, {gun, dummy()});
        const auto attacker = game.worker().id, target = game.units()[1].id, other = game.units()[2].id;
        require(game.attack(std::array{attacker}, target, true), "Forced shot rejected");
        ticks(game, 3); require(game.projectiles().size() == 1, "Shot was not released");
        game.stop(std::array{attacker}); ticks(game, 60);
        require(game.unit(target)->health == 80, "Stop removed permission from an already released shot");
        ticks(game, 300); require(game.attack(std::array{attacker}, target, true), "Second shot rejected");
        ticks(game, 3); require(game.projectiles().size() == 1, "Second shot was not released");
        const_cast<Unit&>(*game.unit(target)).health = 0; game.tick(); ticks(game, 60);
        require(!game.unit(target) && game.unit(other)->health == 100, "Shot retargeted another ally after death");
    });
    test("Splash hits building footprints once and respects ground targeting and friendly fire", [] {
        for (const auto [owner, friendlyFire, targets, expected] : {
            std::tuple{PlayerId{0}, false, AttackTargets::Ground, 1000},
            std::tuple{PlayerId{0}, true, AttackTargets::Ground, 980},
            std::tuple{PlayerId{1}, false, AttackTargets::Ground, 980},
            std::tuple{PlayerId{1}, false, AttackTargets::Air, 1000},
            std::tuple{neutralPlayer, false, AttackTargets::Ground, 1000}}) {
            auto gun = weapon(ProjectileTargeting::Point); gun.projectile->friendlyFire = friendlyFire;
            gun.attackTargets = targets; gun.projectile->speed = 100;
            auto game = makeTestSimulation(range(), {}, gun, {gun});
            auto& hall = const_cast<Building&>(game.buildings().front()); hall.owner = owner;
            require(game.order(std::array{game.worker().id}, OrderKind::AttackGround, hall.origin), "Building bombardment was rejected");
            ticks(game, 10);
            require(hall.health == expected, "Building splash ignored ownership/layer or dealt damage per cell");
        }
    });
    test("Destruction loses the whole queue without refunds and releases its reserved supply once", [] {
        auto gun = weapon(ProjectileTargeting::Point); gun.attackDamage = 2000;
        gun.projectile->friendlyFire = true; gun.projectile->splashRadius = .2f;
        auto scene = range(); scene.slots[0].startingCrystals = 1000;
        auto game = makeTestSimulation(std::move(scene), {}, gun, {gun});
        const auto hall = game.buildings().front().id;
        const Cell origin = game.buildings().front().origin;
        require(game.order(std::array{game.worker().id}, OrderKind::AttackGround, origin), "Could not bombard the depot");
        for (int i = 0; i < 30 && game.projectiles().empty(); ++i) game.tick();
        require(!game.projectiles().empty(), "No building destruction projectile");
        while (game.projectiles().front().elapsedTicks + 1 < game.projectiles().front().flightTicks) game.tick();
        for (int i = 0; i < 10; ++i) require(game.train(hall), "Could not fill destruction queue");
        auto& jobs = const_cast<Building&>(*game.building(hall)).production;
        jobs.front().remainingTicks = 0; // Even a ready job must not spawn after destruction this tick.
        const auto balance = game.storedCrystals();
        require(game.armySupply().used() == 11, "Queue supply fixture wrong");
        game.takeEvents(); game.tick();
        require(!game.building(hall) && game.units().size() == 1 && game.storedCrystals() == balance && game.armySupply().used() == 1,
            "Destruction refunded crystals, retained supply or produced a queued unit");
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x)
            require(game.map().walkable(origin + Cell{x, y}), "Destroyed building retained its footprint");
        const auto events = game.takeEvents();
        require(std::count_if(events.begin(), events.end(), [&](const auto& event) {
            const auto* destroyed = std::get_if<BuildingDestroyed>(&event);
            return destroyed && destroyed->building == hall;
        }) == 1, "Building destruction event missing or duplicated");
        require(!game.cancelTraining(hall) && !game.cancelConstruction(hall), "Destroyed building could still be refunded");
        ticks(game, 10);
        require(game.armySupply().used() == 1 && game.storedCrystals() == balance, "Destruction released supply or refunded twice");
    });
    test("Destroyed construction releases its supply and stops the builder without refunding", [] {
        auto gun = weapon(ProjectileTargeting::Point); gun.attackDamage = 2000;
        gun.projectile->friendlyFire = true; gun.projectile->splashRadius = .2f;
        auto builder = dummy(); builder.canBuild = true; builder.buildableTypes = {"test.structure"};
        EntityDefinition structure; structure.id = "test.structure"; structure.collision = CollisionShape::Footprint; structure.mobile = false; structure.constructible = true; structure.cost = {75, 3}; structure.constructionTicks = 200;
        auto scene = range(); scene.slots[0].startingCrystals = 1000; scene.units = {{builder.id, 0, {6, 6}}};
        auto game = makeTestSimulation(std::move(scene), {}, gun, {gun, builder, structure});
        const auto builderId = game.units().back().id;
        const auto site = game.construct(std::array{builderId}, structure.id, {7, 7});
        require(site.has_value(), "Could not place destructible construction");
        for (int i = 0; i < 100 && game.building(*site)->constructionProgress == 0; ++i) game.tick();
        require(game.building(*site)->constructionProgress > 0 && game.armySupply().used() == 5, "Builder/supply fixture not ready");
        require(game.order(std::array{game.worker().id}, OrderKind::AttackGround, {7, 7}), "Could not bombard construction");
        for (int i = 0; i < 60 && game.building(*site); ++i) game.tick();
        require(!game.building(*site) && game.storedCrystals() == 925 && game.armySupply().used() == 2 && game.map().walkable({7, 7}),
            "Destroyed construction refunded crystals or retained supply/occupancy");
        require(game.unit(builderId)->state == UnitState::Idle && game.unit(builderId)->targetBuilding == 0,
            "Builder retained its destroyed construction target");
    });
    test("Manual siege fire approaches and repeatedly attacks a fixed empty point until stopped", [] {
        auto gun = weapon(ProjectileTargeting::Point); gun.attackCooldownTicks = 8;
        gun.dayVision = gun.nightVision = 2;
        auto game = makeTestSimulation(range(), {}, gun, {gun});
        const rts::Cell target{20, 14}; const auto id = game.worker().id;
        require(!game.fog().explored(target) && game.order(std::array{id}, rts::OrderKind::AttackGround, target), "Point fire required a visible unit target");
        int shots = 0;
        for (int tick = 0; tick < 280; ++tick) {
            game.tick();
            for (const auto& shot : game.projectiles()) {
                require(shot.target == 0 && shot.aim == rts::center(target), "Manual point fire followed a unit or changed its aim");
                if (shot.elapsedTicks == 0) ++shots;
            }
        }
        require(shots > 2 && game.worker().cell != rts::Cell{4, 6}, "Siege did not approach or repeat its order");
        game.stop(std::array{id}); ticks(game, 180);
        require(game.projectiles().empty() && rts::unitOrder(game.worker()) == rts::OrderKind::Stop, "Stopped point fire kept releasing shots");
        auto slinger = weapon(); auto ranged = makeTestSimulation(range(), {}, slinger, {slinger});
        require(!ranged.order(std::array{ranged.worker().id}, rts::OrderKind::AttackGround, {10, 6}), "Non-siege unit accepted point fire");
    });
    test("Ranged windup releases once; damage waits for impact, arc supports straight fire", [] {
        for (float arc : {0.0f,3.0f}) {
            auto gun = weapon(); gun.projectile->arcHeight = arc;
            auto game = stationary(gun); const auto enemy = game.units()[1].id;
            ticks(game,2); require(game.projectiles().empty() && game.unit(enemy)->health == 100,"Early release or damage");
            game.tick(); require(game.projectiles().size() == 1,"Missing projectile");
            const auto shot = game.projectiles().front();
            require(shot.flightTicks == 38 && game.worker().cell == Cell{4,6},"Range or speed ignored");
            ticks(game,19);
            require(std::abs(game.projectiles()[0].height - ((shot.startHeight + shot.aimHeight)*.5f + arc)) < .001f,"Arc peak incorrect");
            ticks(game,18); require(game.unit(enemy)->health == 100,"Damage before landing");
            game.tick(); require(game.projectiles().empty() && game.unit(enemy)->health == 80,"Missing or duplicate impact");
            ticks(game,20); require(game.unit(enemy)->health == 80,"Shot applied twice");
        }
    });
    test("Homing shots follow moving targets; siege fixes its point at release and hits current occupants", [] {
        for (const auto mode : {ProjectileTargeting::Unit,ProjectileTargeting::Point}) {
            auto gun = weapon(mode); auto s = range(); s.worker = {10,6}; s.hall = {20,17};
            auto air = dummy(); air.id = "air"; air.movement = MovementType::Flying;
            s.units = {{gun.id,1,{4,6}}, {"dummy",0,{10,5}}, {"dummy",1,{11,6}}, {air.id,0,{10,6}}};
            auto game = makeTestSimulation(std::move(s),{},dummy(),{gun,dummy(),air});
            const auto moving = game.worker().id, splash = game.units()[2].id, ally = game.units()[3].id, flying = game.units()[4].id;
            ticks(game,3); require(game.projectiles().size() == 1,"Enemy did not fire");
            const auto aim = game.projectiles()[0].aim;
            require(game.command(std::array{moving},{18,6}),"Could not move target");
            ticks(game,38);
            require(game.worker().position.x > aim.x + 2,"Target failed to leave impact area");
            require(game.unit(moving)->health == (mode == ProjectileTargeting::Unit ? 80 : 100),"Wrong homing/point impact");
            require(game.unit(splash)->health == (mode == ProjectileTargeting::Point ? 80 : 100),"Splash does not use current area occupants");
            require(game.unit(ally)->health == 100 && game.unit(flying)->health == 100,"Splash hit ally or air");
        }
    });
    test("Projectile outlives shooter and still grants kill experience to its army", [] {
        auto gun = weapon(); gun.maximumHealth = 1; gun.attackDamage = 100;
        auto killer = dummy(); killer.id = "killer"; killer.attackDamage = 100; killer.projectile.reset();
        killer.attackRange = 1.5f; killer.attackWindupTicks = 4; killer.dayVision = killer.nightVision = 2;
        auto hero = dummy(); hero.id = "hero"; hero.hero.emplace();
        auto s = range(); s.units = {{"dummy",1,{10,6}}, {killer.id,1,{4,7}}, {hero.id,0,{10,10}}};
        auto game = makeTestSimulation(std::move(s),{},gun,{gun,dummy(),killer,hero});
        const auto shooter = game.worker().id, enemy = game.units()[1].id;
        require(game.attack(std::array{shooter},enemy),"Explicit shot rejected");
        ticks(game,5); require(!game.unit(shooter) && game.projectiles().size() == 1,"Shot vanished with shooter");
        ticks(game,36); require(!game.unit(enemy) && game.hero()->hero->experience == 50,"Posthumous hit or XP lost");
    });
    test("Shot to a dead target expires harmlessly without retargeting", [] {
        auto gun = weapon(); auto killer = dummy(); killer.id = "killer"; killer.projectile.reset();
        killer.attackDamage = 100; killer.attackRange = 1.5f; killer.attackWindupTicks = 4;
        auto s = range(); s.units = {{"dummy",1,{10,6}}, {killer.id,0,{9,6}}, {"dummy",1,{10,7}}};
        auto game = makeTestSimulation(std::move(s),{},gun,{gun,killer,dummy()});
        const auto enemy = game.units()[1].id, other = game.units()[3].id, killerId = game.units()[2].id;
        game.attack(std::array{game.worker().id,killerId},enemy);
        ticks(game,5); require(!game.unit(enemy) && !game.projectiles().empty(),"Fixture failed to kill target in flight");
        game.stop(std::array{killerId}); // Its long cooldown prevents another hit.
        ticks(game,38); require(game.projectiles().empty() && game.unit(other)->health == 100,"Shot retargeted");
    });
    test("Orders interrupt preparation but cannot bypass recovery and cooldown", [] {
        auto gun = weapon(); gun.attackCooldownTicks = 4;
        auto game = stationary(gun); const auto id = game.worker().id, enemy = game.units()[1].id;
        ticks(game,2); game.stop(std::array{id});
        require(game.projectiles().empty() && game.worker().attackPhase == AttackPhase::Ready,"Cancelled windup released");
        game.attack(std::array{id},enemy); ticks(game,3); require(game.projectiles().size() == 1,"Restart failed");
        game.stop(std::array{id}); game.attack(std::array{id},enemy);
        require(game.worker().attackTicks == 7,"Post-release delay reset by orders");
        ticks(game,8); require(game.projectiles().size() == 1,"Order accelerated next shot");
        game.tick(); require(game.projectiles().size() == 2,"Next shot never arrived");
    });
    test("Weapon masks allow slingers versus air while siege rejects air and fog hides targets", [] {
        auto gun = weapon(); auto air = dummy(); air.id = "air"; air.movement = MovementType::Flying;
        auto s = range(); s.units = {{air.id,1,{10,6}}};
        auto game = makeTestSimulation(s,{},gun,{gun,air});
        require(game.attack(std::array{game.worker().id},game.units()[1].id),"Slinger rejected air");
        ticks(game,41); require(game.units()[1].health == 80,"Arrow did not reach flight plane");
        gun = weapon(ProjectileTargeting::Point); auto siege = makeTestSimulation(s,{},gun,{gun,air});
        require(!siege.attack(std::array{siege.worker().id},siege.units()[1].id),"Siege accepted air");
        ticks(siege,40); require(siege.projectiles().empty(),"Siege auto-acquired air");
        s.units = {{"dummy",1,{23,19}}}; gun.dayVision = gun.nightVision = 2;
        auto fog = makeTestSimulation(s,{},gun,{gun,dummy()});
        require(!fog.attack(std::array{fog.worker().id},fog.units()[1].id),"Attack leaked fog target");
    });
}
}
