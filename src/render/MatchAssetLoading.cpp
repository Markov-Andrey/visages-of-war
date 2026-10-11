#include "RenderSupport.hpp"
#include "rts/MatchAssets.hpp"

namespace rts {
using namespace render;
bool Renderer::prepareMatchAssets(const Simulation& game, const Definitions& definitions,
    const std::function<bool(const std::wstring&, size_t, size_t)>& progress,
    std::span<const std::string> additionalEntities) {
    ensureTarget();
    const auto plan = collectMatchAssets(game, definitions, additionalEntities);
    // Reuse an identical match's blocks. Drop previous race/library resources
    // when the required set changes, including the large CPU atlases.
    if (!residentMatchAssets_ || *residentMatchAssets_ != plan) {
        groundBrush_.Reset();
        reloadWorldAssets(worldAssets_);
    }
    rallySprites_.clear();
    using Task = std::pair<std::wstring, std::function<void()>>;
    std::vector<Task> tasks;
    const auto add = [&](std::wstring label, std::function<void()> work) { tasks.emplace_back(std::move(label), std::move(work)); };
    const auto masked = [&](const std::string& image, const std::string& mask, unsigned color, const std::wstring& label) {
        if (!image.empty()) add(label, [=, this] { maskedBitmap(image, mask, color); });
    };
    const auto uiImageTask = [&](const std::string& image, const std::string& mask, unsigned color, const std::wstring& label) {
        if (image.empty()) return;
        if (!mask.empty()) { masked(image, mask, color, label); return; }
        add(label, [=, this] {
            const auto path = imagePath(image);
            auto& bitmap = unitUiImages_[path];
            if (!bitmap) loadBitmap(paths_.asset(path), bitmap);
        });
    };
    const auto worldImage = [&](std::filesystem::path path, bool highlights = false) {
        add(L"Окружение карты", [=, this] {
            auto& bitmap = worldSprites_[path];
            if (!bitmap) loadBitmap(paths_.asset(path), bitmap, 0, SpriteTeamMask::None, {}, true, highlights);
        });
    };
    for (const auto& id : plan.materials) add(L"Поверхность земли", [=, this] { materialResource(id); });
    for (const auto& id : plan.objects) add(L"Деревья и декорации", [=, this] {
        const auto& object = worldAssets_.object(id);
        if (!object.image.empty()) worldBitmap(object);
    });
    for (const auto& id : plan.crystals) worldImage(worldAssets_.crystalSprite(id).image, true);
    worldImage(worldAssets_.bonesSprite().image);
    const unsigned localColor = teamRgb(game.player().color);
    for (const auto& [id, path] : worldAssets_.resourceIcons()) add(L"Интерфейс", [=, this] {
        auto& bitmap = unitUiImages_[path];
        if (!bitmap) loadBitmap(paths_.asset(path), bitmap);
    });
    for (const auto& [id, resource] : commandIcons_) add(L"Интерфейс приказов", [=, this] {
        auto& icon = commandIcons_.at(id);
        const auto color = icon.definition.mask.empty() ? 0 : localColor;
        if (!icon.bitmap || icon.color != color) loadBitmap(icon.definition.image, icon.bitmap, color, SpriteTeamMask::None, icon.definition.mask);
        icon.color = color;
    });
    for (const auto& [faction, block] : plan.factions) {
        const auto label = L"Раса: " + wide(definitions.faction(faction).displayName);
        for (const auto& id : block.entities) {
            const auto& type = definitions.entity(id);
            for (unsigned color : block.colors) {
                const auto sprite = type.sprite;
                if (!sprite.image.empty()) add(label, [=, this] { unitBitmap(sprite, color); });
                uiImageTask(sprite.portrait, sprite.portraitMask, color, label);
                uiImageTask(sprite.icon, sprite.iconMask, color, label);
                if (sprite.death) {
                    const auto death = *sprite.death;
                    if (!death.teamMask.empty()) masked(death.image, death.teamMask, color, label);
                    else add(label, [=, this] {
                        const auto tint = sprite.teamMask == SpriteTeamMask::None ? 0 : color;
                        auto& bitmap = unitSheets_[{imagePath(death.image), tint, sprite.teamMask}];
                        if (!bitmap) loadBitmap(paths_.asset(imagePath(death.image)), bitmap, tint, sprite.teamMask, {}, true);
                    });
                }
                if (type.projectile) masked(type.projectile->image, type.projectile->teamMask, color, label);
                const auto& building = type.buildingSprite;
                uiImageTask(building.portrait, building.portraitMask, color, label);
                uiImageTask(building.icon, building.iconMask, color, label);
                const auto art = [&](const auto& layer) {
                    masked(layer.image, layer.teamMask, color, label);
                    if (!layer.emissionMask.empty()) add(label, [image = layer.emissionMask, this] { emissionBitmap(image); });
                };
                for (const auto& stage : building.stages) {
                    art(stage);
                    for (const auto& layer : stage.layers) art(layer);
                }
            }
            if (type.buildingSprite.construction) {
                masked(type.buildingSprite.construction->image, {}, 0, label);
                add(L"Эффект строительства", [=, &definitions, this] { constructionResource(definitions.entity(id)); });
            }
        }
    }
    const auto& commander = definitions.commander(game.player().commanderId);
    if (commander.rallySprite) {
        rallySprites_[commander.id] = *commander.rallySprite;
        masked(commander.rallySprite->image, commander.rallySprite->teamMask, localColor, L"Точка сбора");
    }
    add(L"Рельеф и туман войны", [&, this] { prepareLandscape(game.landscape(), game.map()); updateFogMask(game); });
    for (size_t i = 0; ; ) {
        const auto previousTarget = target_;
        if (!progress(i == tasks.size() ? L"Готово" : tasks[i].first, i, tasks.size())) return false;
        ensureTarget();
        // A DPI/resize/device change may discard all uploaded resources while
        // the loading UI pumps messages. Revisit tasks so the match starts warm.
        if (previousTarget.Get() != target_.Get()) { i = 0; continue; }
        if (i == tasks.size()) { residentMatchAssets_ = plan; return true; }
        tasks[i++].second();
    }
}
}
