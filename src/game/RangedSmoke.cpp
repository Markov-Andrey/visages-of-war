#include "GameApplication.hpp"
#include <stdexcept>

namespace rts::game {
void GameApplication::exerciseRangedCombat() {
    rts::Scenario scene{rts::Map(32,32),{10,10},{12,14},{}};
    scene.units = {{"human.archer",0,{14,14}}, {"human.catapult",0,{14,16}}, {"human.soldier",1,{20,14}}};
    auto types = definitions_.entities();
    for (auto& d : types) if (d.id == "human.soldier") { d.attackDamage = 0; d.maximumHealth = 10000; }
    game_ = rts::Simulation(std::move(scene),menu_.player,definitions_.entity("human.worker"),std::move(types));
    ui_ = {}; ui_.selection.army(game_);
    const auto enemy = game_.units().back().id;
    if (!game_.attack(ui_.selection.ids,enemy)) throw std::runtime_error("Ranged smoke: attack rejected");
    for (int t = 0; t < 28; ++t) game_.tick();
    if (game_.projectiles().size() != 2 || game_.unit(enemy)->health != 10000)
        throw std::runtime_error("Ranged smoke: expected two airborne shots before impact");
    renderer_.snapshot(game_,rts::Paths::executable().parent_path()/L"ranged-preview.png",nullptr,false,&ui_);
    for (int t = 0; t < 50; ++t) game_.tick();
    if (game_.unit(enemy)->health >= 10000 - 45) throw std::runtime_error("Ranged smoke: no projectile damage");
    rts::Scenario training{rts::Map(32,32),{10,10},{12,14},{}};
    training.startingCrystals = 1000;
    types = definitions_.entities();
    for (auto& d : types) if (d.id == "human.barracks") d.constructionTicks = 1;
    game_ = rts::Simulation(std::move(training),menu_.player,definitions_.entity("human.worker"),std::move(types));
    const auto site = game_.construct(std::array{game_.worker().id},"human.barracks",{14,14});
    if (!site) throw std::runtime_error("Ranged smoke: barracks construction failed");
    for (int t = 0; t < 120; ++t) game_.tick();
    ui_ = {}; ui_.selection.ids = {*site};
    onMessage(WM_KEYDOWN,'E',0); onMessage(WM_KEYDOWN,'T',0);
    const auto& jobs = game_.building(*site)->production;
    if (jobs.size() != 2 || jobs[0].definitionId != "human.archer" || jobs[1].definitionId != "human.catapult")
        throw std::runtime_error("Ranged smoke: recruitment keys ignored roster");
    renderer_.snapshot(game_,rts::Paths::executable().parent_path()/L"ranged-training-preview.png",nullptr,false,&ui_);
}
}
