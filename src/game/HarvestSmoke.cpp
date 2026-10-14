#include "GameApplication.hpp"
#include "rts/HarvestVisual.hpp"
#include <stdexcept>

namespace rts::game {
void GameApplication::exerciseHarvestFeedback() {
    const auto& type = definitions_.entity("church.worker");
    Scenario scene{Map(24, 24), {3, 3}, {11, 12}, {worldAssets_.instantiateCrystal("crystal.small", {12, 12})}};
    scene.slots[menu_.player.id].startingCrystals = 0;
    game_ = Simulation(std::move(scene), menu_.player, type, definitions_.entities(), definitions_.startingDepot(type.factionId).id, {}, definitions_.progression(), {}, definitions_.armorRules());
    ui_ = {}; ui_.selection.ids = {game_.worker().id};
    if (!game_.command({12, 12})) throw std::runtime_error("Harvest smoke: command rejected");
    for (int i = 0; i < 180 && game_.worker().state != UnitState::Harvesting; ++i) { game_.tick(); processWorldEvents(); }
    if (!harvestEffectTarget(game_, game_.worker())) throw std::runtime_error("Harvest smoke: no active channel");
    const WorldView closeup{{}, 1.8f};
    auto camera = closeup;
    camera.origin = Vec2{720, 390} - camera.project(game_.worker().position, game_.workerHeight());
    const auto snapshot = [&](const wchar_t* name) {
        renderer_.snapshot(game_, Paths::executable().parent_path() / name, nullptr, false, &ui_, MenuPage::Playing, 0, nullptr, &camera);
    };
    for (int i = 1; i < type.harvesting().intervalTicks; ++i) { game_.tick(); processWorldEvents(); }
    if (game_.storedCrystals() || !ui_.floatingNotifications.entries().empty())
        throw std::runtime_error("Harvest smoke: premature credit or popup");
    snapshot(L"novice-harvesting-preview.png");
    game_.tick(); processWorldEvents();
    if (game_.storedCrystals() != 7 || ui_.floatingNotifications.entries().size() != 1 ||
        ui_.floatingNotifications.entries().front().text != L"+7") throw std::runtime_error("Harvest smoke: missing income popup");
    snapshot(L"novice-income-preview.png");
    for (int i = 0; i < 45; ++i) { game_.tick(); processWorldEvents(); }
    snapshot(L"novice-income-fading-preview.png");
    game_.stop();
    if (harvestEffectTarget(game_, game_.worker())) throw std::runtime_error("Harvest smoke: Stop retained channel");
    for (int i = 0; i < 20; ++i) { game_.tick(); processWorldEvents(); }
    if (!ui_.floatingNotifications.entries().empty()) throw std::runtime_error("Harvest smoke: expired popup retained");
}
}
