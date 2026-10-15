#include "TestSupport.hpp"
#include <entt/entity/registry.hpp>
#include <type_traits>

namespace rts::tests {
namespace {
struct Visits { int count{}; };
}
void ecsTests(TestSuite& test) {
    test("ECS stores one component state and keeps entity addresses through growth and removal", [] {
        ecs::World world;
        const auto type = fixtureWorker();
        auto* first = &world.create(1, type);
        first->vitals().health = 12.34;
        mustThrow([&] { world.create(0, type); });
        mustThrow([&] { world.create(1, type); });
        for (EntityId id = 2; id <= 2049; ++id) world.create(id, type);
        require(world.find(1) == first && first->vitals().health == Vital(12.34), "Pool growth invalidated an entity");
        for (auto&& [handle, entity, vitals] : world.registry().view<Entity, components::Vitals>().each()) {
            require(&vitals == &entity.vitals(), "Facade and ECS have different state");
            if (entity.id == 1) world.registry().emplace<Visits>(handle, 7);
        }
        require(world.destroy(1024) && !world.destroy(1024) && !world.find(1024), "Removed ID remains live");
        require(world.find(1) == first && first->vitals().health == Vital(12.34), "Removal corrupted another entity");
        require(world.circles().size() == 2048 && world.circles()[1023].id == 1025, "Shape index lost insertion order");
        size_t live = 0;
        for (auto&& [handle, entity] : world.registry().view<Entity>().each()) {
            require(world.find(entity.id) == &entity, "Registry and ID index disagree"); ++live;
        }
        require(live == 2048, "View visited a deleted entity");
        require(world.destroy(1) && world.registry().view<Visits>().empty(), "Custom component outlived its entity");
        world.create(2050, type);
        require(!world.find(1), "Recycled ECS slot resurrected a stale gameplay ID");
        static_assert(std::is_same_v<decltype(std::as_const(world).circles().front()), const Entity&>);
    });
    test("ECS world moves retain registry ownership and component access", [] {
        ecs::World source;
        auto* entity = &source.create(5, fixtureWorker());
        entity->vitals().mana = 3.21;
        const auto* registry = &source.registry();
        ecs::World moved(std::move(source));
        ecs::World assigned;
        assigned.create(9, fixtureWorker());
        assigned = std::move(moved);
        require(&assigned.registry() == registry && assigned.find(5) == entity && !assigned.find(9), "Move changed ownership");
        require(entity->vitals().mana == Vital(3.21), "Entity retained a pointer to the moved-from world");
    });
    test("ECS module phases are deterministic and reject ambiguous or recursive registration", [] {
        ecs::World world; Map map(8, 8); PlayerSlots players{};
        std::array<PlayerEconomy, maximumPlayers> economies{};
        ecs::SystemContext context{world, map, players, economies, 1};
        ecs::Systems systems;
        std::vector<int> calls;
        systems.add("zeta", ecs::Phase::BeforeOrders, [&](auto&) { calls.push_back(3); });
        systems.add("alpha", ecs::Phase::BeforeOrders, [&](auto&) { calls.push_back(2); });
        systems.add("early", ecs::Phase::BeforeOrders, [&](auto&) { calls.push_back(1); }, -1);
        systems.add("late", ecs::Phase::AfterProduction, [&](auto&) { calls.push_back(4); });
        mustThrow([&] { systems.add("alpha", ecs::Phase::AfterCombat, [](auto&) {}); });
        mustThrow([&] { systems.add("", ecs::Phase::BeforeOrders, [](auto&) {}); });
        mustThrow([&] { systems.add("empty", ecs::Phase::BeforeOrders, {}); });
        require(!systems.run(ecs::Phase::AfterOrders, context), "Empty phase reported work");
        systems.run(ecs::Phase::BeforeOrders, context);
        systems.run(ecs::Phase::AfterProduction, context);
        require(calls == std::vector<int>({1, 2, 3, 4}), "Order depends on registration order");
        systems.add("guard", ecs::Phase::AfterCombat, [&](auto&) {
            mustThrow([&] { systems.add("illegal", ecs::Phase::BeforeOrders, [](auto&) {}); });
            mustThrow([&] { systems.run(ecs::Phase::AfterCombat, context); });
            throw std::runtime_error("Module failure");
        });
        mustThrow([&] { systems.run(ecs::Phase::AfterCombat, context); });
        require(systems.run(ecs::Phase::BeforeOrders, context), "Exception left the scheduler locked");
    });
    test("ECS match modules share the entity model across troops buildings and heroes after moves", [] {
        auto hero = fixtureWorker(); hero.id = "test.hero"; hero.hero.emplace();
        auto scene = flatScenario(); scene.heroSpawn = Cell{5, 5};
        auto source = makeTestSimulation(std::move(scene), {}, fixtureWorker(), {hero}, hero.id);
        const auto workerId = source.worker().id;
        const auto* worker = source.entity(workerId);
        source.addSystem("test.visits", ecs::Phase::BeforeOrders, [](ecs::SystemContext& context) {
            auto& registry = context.entities.registry();
            for (auto&& [handle, entity] : registry.view<Entity>().each())
                ++registry.get_or_emplace<Visits>(handle).count;
        });
        auto game = std::move(source);
        ticks(game, 2);
        require(game.entity(workerId) == worker && game.hero(), "Moving the match lost actors");
        size_t visited = 0;
        for (auto&& [handle, entity, visits] : game.world().registry().view<const Entity, const Visits>().each()) {
            require(visits.count == 2, "Module skipped an entity or ran twice per tick"); ++visited;
        }
        require(visited == game.units().size() + game.buildings().size(), "Module was restricted to a physical shape");
        auto other = makeTestSimulation(flatScenario());
        other.tick();
        require(other.world().registry().view<const Visits>().empty(), "Component or system state leaked between matches");
    });
    test("ECS module deaths use normal supply footprint event and observer cleanup", [] {
        auto game = makeTestSimulation(flatScenario());
        const auto worker = game.worker().id;
        const auto building = game.buildings().front().id;
        const auto origin = game.buildings().front().origin;
        game.takeEvents();
        game.addSystem("test.death", ecs::Phase::AfterOrders, [](ecs::SystemContext& context) {
            for (auto&& [handle, vitals] : context.entities.registry().view<components::Vitals>().each()) vitals.health = 0;
        });
        bool cleaned = false;
        game.addSystem("test.after-death", ecs::Phase::AfterCombat, [&](ecs::SystemContext& context) {
            cleaned = !context.entities.find(worker) && !context.entities.find(building);
        });
        game.tick();
        require(cleaned && game.units().empty() && game.buildings().empty(), "Phase left dead actors in ECS");
        require(game.armySupply().used() == 0 && game.map().walkable(origin), "Death leaked supply or footprint");
        const auto events = game.takeEvents();
        require(std::count_if(events.begin(), events.end(), [](const auto& e) { return std::holds_alternative<UnitDied>(e); }) == 1 &&
            std::count_if(events.begin(), events.end(), [](const auto& e) { return std::holds_alternative<BuildingDestroyed>(e); }) == 1,
            "Death event missing or duplicated");
        game.tick();
        require(game.takeEvents().empty(), "Deleted actors continued producing events");
    });
}
}
