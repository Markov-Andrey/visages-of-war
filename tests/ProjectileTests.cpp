#include "TestSupport.hpp"

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
    return Simulation(std::move(s),{},gun,{gun,dummy()});
}
}
void projectileTests(TestSuite& test, const TestContext& context) {
    test("Projectile rendering follows shifted units but preserves ordered ground impacts", [] {
        Projectile shot;
        shot.flightTicks = 10; shot.position = {8.5f, 6.5f}; shot.previousPosition = {8, 6.5f};
        shot.height = 3; shot.previousHeight = 2.5f;
        for (float zoom : {.4f, 1.0f, 1.8f}) for (auto mode : {ProjectileTargeting::Unit, ProjectileTargeting::Point}) {
            const WorldView view{{170, -60}, zoom};
            shot.definition.targeting = mode;
            for (int tick : {0, 5, 10}) {
                shot.elapsedTicks = tick;
                for (bool previous : {false, true}) {
                    const auto logical = view.project(previous ? shot.previousPosition : shot.position,
                        previous ? shot.previousHeight : shot.height);
                    const auto visual = projectileScreenPosition(view, shot, previous);
                    const float progress = float(previous ? std::max(0, tick - 1) : tick) / 10;
                    const float offset = WorldView::tileSize * zoom / 3 * (mode == ProjectileTargeting::Unit ? 1 : 1 - progress);
                    require(std::abs(visual.x - logical.x) < .001f && std::abs(visual.y - logical.y - offset) < .001f,
                        "Projectile detached from the shooter/target or displaced a ground impact");
                }
            }
        }
        require(shot.position == Vec2{8.5f, 6.5f} && shot.previousPosition == Vec2{8, 6.5f} && shot.height == 3,
            "Projectile presentation changed the simulation trajectory");
    });
    test("Manual siege fire approaches and repeatedly attacks a fixed empty point until stopped", [] {
        auto gun = weapon(ProjectileTargeting::Point); gun.attackCooldownTicks = 8;
        gun.dayVision = gun.nightVision = 2;
        rts::Simulation game(range(), {}, gun, {gun});
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
        auto archer = weapon(); rts::Simulation bow(range(), {}, archer, {archer});
        require(!bow.order(std::array{bow.worker().id}, rts::OrderKind::AttackGround, {10, 6}), "Non-siege unit accepted point fire");
    });
    test("Manual point fire uses splash masks and preserves friendly fire settings", [] {
        for (const bool friendly : {false, true}) {
            auto gun = weapon(ProjectileTargeting::Point); gun.projectile->friendlyFire = friendly;
            auto air = dummy(); air.id = "air"; air.movement = MovementType::Flying;
            auto scene = range(); scene.units = {{"dummy", 1, {10, 6}}, {"dummy", 0, {10, 7}}, {"air", 1, {10, 6}}};
            Simulation game(scene, {}, gun, {gun, dummy(), air});
            const auto enemy = game.units()[1].id, ally = game.units()[2].id, flyer = game.units()[3].id;
            require(game.order(std::array{game.worker().id}, OrderKind::AttackGround, {10, 6}), "Manual siege order rejected");
            ticks(game, 34);
            require(game.unit(enemy)->health == 80 && game.unit(ally)->health == (friendly ? 80 : 100) && game.unit(flyer)->health == 100,
                "Manual siege splash bypassed weapon target or friendly-fire rules");
        }
    });
    test("Ranged windup releases once; damage waits for impact, arc supports straight fire", [] {
        for (float arc : {0.0f,3.0f}) {
            auto gun = weapon(); gun.projectile->arcHeight = arc;
            auto game = stationary(gun); const auto enemy = game.units()[1].id;
            ticks(game,2); require(game.projectiles().empty() && game.unit(enemy)->health == 100,"Early release or damage");
            game.tick(); require(game.projectiles().size() == 1,"Missing projectile");
            const auto shot = game.projectiles().front();
            require(shot.flightTicks == 30 && game.worker().cell == Cell{4,6},"Range or speed ignored");
            ticks(game,15);
            require(std::abs(game.projectiles()[0].height - ((shot.startHeight + shot.aimHeight)*.5f + arc)) < .001f,"Arc peak incorrect");
            ticks(game,14); require(game.unit(enemy)->health == 100,"Damage before landing");
            game.tick(); require(game.projectiles().empty() && game.unit(enemy)->health == 80,"Missing or duplicate impact");
            ticks(game,20); require(game.unit(enemy)->health == 80,"Shot applied twice");
        }
    });
    test("Homing arrows follow moving targets; siege fixes its point at release and hits current occupants", [] {
        for (const auto mode : {ProjectileTargeting::Unit,ProjectileTargeting::Point}) {
            auto gun = weapon(mode); auto s = range(); s.worker = {10,6};
            auto air = dummy(); air.id = "air"; air.movement = MovementType::Flying;
            s.units = {{gun.id,1,{4,6}}, {"dummy",0,{10,7}}, {"dummy",1,{11,6}}, {air.id,0,{10,6}}};
            Simulation game(std::move(s),{},dummy(),{gun,dummy(),air});
            const auto moving = game.worker().id, splash = game.units()[2].id, ally = game.units()[3].id, flying = game.units()[4].id;
            ticks(game,3); require(game.projectiles().size() == 1,"Enemy did not fire");
            const auto aim = game.projectiles()[0].aim;
            require(game.command(std::array{moving},{18,6}),"Could not move target");
            ticks(game,30);
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
        Simulation game(std::move(s),{},gun,{gun,dummy(),killer,hero});
        const auto shooter = game.worker().id, enemy = game.units()[1].id;
        require(game.attack(std::array{shooter},enemy),"Explicit shot rejected");
        ticks(game,5); require(!game.unit(shooter) && game.projectiles().size() == 1,"Shot vanished with shooter");
        ticks(game,28); require(!game.unit(enemy) && game.hero()->hero->experience == 50,"Posthumous hit or XP lost");
    });
    test("Siege samples a moving target at release and uses the actual ramp surface", [] {
        auto gun = weapon(ProjectileTargeting::Point); gun.attackWindupTicks = 6; gun.projectile->impactHeight = 0;
        auto target = dummy(); target.movementPerSecond = 3;
        auto s = range(); s.worker = {10,6}; s.units = {{gun.id,1,{4,6}}};
        for (int y = 0; y < s.map.height(); ++y) for (int x = 12; x < s.map.width(); ++x) s.map.at({x,y}).height = 1;
        s.map.at({11,6}).ramp = {1,0};
        Simulation game(std::move(s),{},target,{gun,target});
        require(game.command(std::array{game.worker().id},{14,6}),"Ramp movement failed");
        ticks(game,7); require(game.projectiles().size() == 1,"Siege failed to release at moving target");
        const auto& shot = game.projectiles()[0];
        require(std::abs(shot.aim.x - game.worker().position.x) < .001f && shot.aim.x > 11,"Aim fixed before release");
        require(std::abs(shot.aimHeight - game.unitHeight(game.worker())) < .001f && shot.aimHeight > 0,"Impact used reserved cell rather than ramp position");
    });
    test("Arrow to a dead target expires harmlessly without retargeting", [] {
        auto gun = weapon(); auto killer = dummy(); killer.id = "killer"; killer.projectile.reset();
        killer.attackDamage = 100; killer.attackRange = 1.5f; killer.attackWindupTicks = 4;
        auto s = range(); s.units = {{"dummy",1,{10,6}}, {killer.id,0,{9,6}}, {"dummy",1,{10,7}}};
        Simulation game(std::move(s),{},gun,{gun,killer,dummy()});
        const auto enemy = game.units()[1].id, other = game.units()[3].id, killerId = game.units()[2].id;
        game.attack(std::array{game.worker().id,killerId},enemy);
        ticks(game,5); require(!game.unit(enemy) && !game.projectiles().empty(),"Fixture failed to kill target in flight");
        game.stop(std::array{killerId}); // Its long cooldown prevents another hit.
        ticks(game,30); require(game.projectiles().empty() && game.unit(other)->health == 100,"Arrow retargeted");
    });
    test("Attack phases keep windup, recovery and cooldown distinct including zero-duration phases", [] {
        for (const auto times : {std::array{2,3,4},std::array{0,3,4},std::array{2,0,4},std::array{0,0,4},std::array{2,3,0},std::array{2,0,0}}) {
            auto gun = weapon(); gun.attackWindupTicks = times[0]; gun.attackRecoveryTicks = times[1]; gun.attackCooldownTicks = times[2];
            auto game = stationary(gun); std::vector<int> releases;
            for (int t = 1; t <= 28; ++t) {
                game.tick();
                for (const auto& p : game.projectiles()) if (p.elapsedTicks == 0) releases.push_back(t);
            }
            require(releases.size() >= 2 && releases[0] == 1 + times[0],"Initial preparation incorrect");
            for (size_t i = 1; i < releases.size(); ++i)
                require(releases[i] - releases[i-1] == times[0] + times[1] + times[2],"Attack interval differs from phase sum");
        }
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
    test("Weapon masks allow archers versus air while siege rejects air and fog hides targets", [] {
        auto gun = weapon(); auto air = dummy(); air.id = "air"; air.movement = MovementType::Flying;
        auto s = range(); s.units = {{air.id,1,{10,6}}};
        Simulation game(s,{},gun,{gun,air});
        require(game.attack(std::array{game.worker().id},game.units()[1].id),"Archer rejected air");
        ticks(game,40); require(game.units()[1].health == 80,"Arrow did not reach flight plane");
        gun = weapon(ProjectileTargeting::Point); Simulation siege(s,{},gun,{gun,air});
        require(!siege.attack(std::array{siege.worker().id},siege.units()[1].id),"Siege accepted air");
        ticks(siege,40); require(siege.projectiles().empty(),"Siege auto-acquired air");
        s.units = {{"dummy",1,{23,19}}}; gun.dayVision = gun.nightVision = 2;
        Simulation fog(s,{},gun,{gun,dummy()});
        require(!fog.attack(std::array{fog.worker().id},fog.units()[1].id),"Attack leaked fog target");
    });
    test("Combat catalog validates trajectories, animation and optional melee projectile", [&] {
        CatalogFixture original(context.assets);
        const auto defs = original.load();
        require(!defs.entity("human.peacemaker").projectile && defs.entity("human.archer").projectile->targeting == ProjectileTargeting::Unit &&
            defs.entity("human.catapult").projectile->targeting == ProjectileTargeting::Point,"Catalog weapon modes incorrect");
        const auto rejects = [&](const std::function<void(Json&)>& change) {
            auto f = original; change(f.entities["entities"][7]); mustThrow([&] { f.load(); });
        };
        rejects([](Json& e) { e["attack"]["projectile"]["speed"] = 0; });
        rejects([](Json& e) { e["attack"]["projectile"]["arcHeight"] = -1; });
        rejects([](Json& e) { e["attack"]["range"] = 0; });
        rejects([](Json& e) { e["attack"]["windupTicks"] = .5; });
        rejects([](Json& e) { for (auto name : {"windupTicks","recoveryTicks","cooldownTicks"}) e["attack"][name] = 0; });
        rejects([](Json& e) { e["attack"]["targets"] = "anything"; });
        rejects([](Json& e) { e["attack"]["projectile"]["image"] = "../outside.png"; });
        rejects([](Json& e) { e["sprite"]["image"] = "C:/outside.png"; });
        rejects([](Json& e) { e["sprite"]["windup"] = Json::array(); });
        rejects([](Json& e) { e["attack"]["projectile"]["targeting"] = "point"; });
        rejects([](Json& e) { e["attack"]["projectile"]["splashRadius"] = 2; });
    });
    test("Barracks roster trains selected ranged types with independent costs, supply and refunds", [&] {
        const auto defs = Definitions::load(context.assets / "data/catalog.json");
        auto s = range(); s.startingCrystals = 1000;
        auto types = defs.entities();
        for (auto& d : types) if (d.id == "human.barracks") d.constructionTicks = 1;
        Simulation game(std::move(s),{},defs.entity("human.worker"),types);
        const auto site = game.construct(std::array{game.worker().id},"human.barracks",{6,8});
        require(site.has_value(),"Barracks fixture failed"); ticks(game,180);
        const int crystals = game.storedCrystals(), supply = game.armySupply().used();
        require(game.train(*site,"human.archer") && game.train(*site,"human.catapult"),"Ranged roster unavailable");
        require(!game.train(*site,"human.hero"),"Building trained an unsupported type");
        require(game.storedCrystals() == crystals - 240 && game.armySupply().used() == supply + 6,"Roster costs wrong");
        require(game.cancelTraining(*site) && game.storedCrystals() == crystals - 80 && game.armySupply().used() == supply + 2,"Roster refund wrong");
        ticks(game,180);
        require(game.units().back().definitionId == "human.archer","Production ignored selected type");
        require(game.train(*site,"human.catapult"),"Siege recruitment failed"); ticks(game,300);
        require(game.units().back().definitionId == "human.catapult","Production lost siege definition");
    });
}
}
