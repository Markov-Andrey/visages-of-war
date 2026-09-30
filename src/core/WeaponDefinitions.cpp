#include "DefinitionData.hpp"

namespace rts::data {
namespace {
std::string imagePath(const Json& j) {
    const auto value = string(j);
    const std::filesystem::path path{std::u8string(value.begin(), value.end())};
    if (path.has_root_path() || value.find(':') != std::string::npos || value.find('\0') != std::string::npos) throw std::runtime_error("Sprite path must be relative to assets");
    for (const auto& component : path) if (component == L"..") throw std::runtime_error("Sprite path escapes assets");
    return value;
}
Vec2 pair(const Json& j, float low, float high) {
    if (!j.is_array() || j.size() != 2) throw std::runtime_error("Expected two components");
    return {real(j[0], low, high), real(j[1], low, high)};
}
std::vector<int> frames(const Json& j) {
    if (!j.is_array() || j.empty() || j.size() > 128) throw std::runtime_error("Animation needs 1..128 frames");
    std::vector<int> result;
    for (const auto& frame : j) result.push_back(number(frame, 0, 255));
    return result;
}
std::array<int, 4> sourceRectangle(const Json& j) {
    if (!j.is_array() || j.size() != 4) throw std::runtime_error("Sprite source requires four components");
    std::array<int, 4> result;
    for (size_t i = 0; i < 4; ++i) result[i] = number(j[i], i < 2 ? 0 : 1, 8192);
    return result;
}
BuildingSpriteLayer buildingLayer(const Json& j) {
    fields(j, {"image", "frames", "destination"}, {"teamMask", "ticksPerFrame", "phase", "when"});
    BuildingSpriteLayer layer;
    layer.image = imagePath(j.at("image"));
    if (j.contains("teamMask") && !j.at("teamMask").is_null()) layer.teamMask = imagePath(j.at("teamMask"));
    const auto& frames = j.at("frames");
    if (!frames.is_array() || frames.empty() || frames.size() > 128) throw std::runtime_error("Building layer requires 1..128 frames");
    for (const auto& source : frames) layer.frames.push_back(sourceRectangle(source));
    const auto& destination = j.at("destination");
    if (!destination.is_array() || destination.size() != 4) throw std::runtime_error("Building layer destination requires four components");
    for (size_t i = 0; i < 4; ++i) layer.destination[i] = real(destination[i], i < 2 ? -8192.f : .01f, 8192);
    if (j.contains("ticksPerFrame")) layer.ticksPerFrame = number(j.at("ticksPerFrame"), 1, 3600);
    if (j.contains("phase")) layer.phase = number(j.at("phase"), 0, static_cast<int>(layer.frames.size()) - 1);
    if (j.contains("when")) {
        const auto when = string(j.at("when"));
        if (when == "training") layer.when = BuildingLayerWhen::Training;
        else if (when != "always") throw std::runtime_error("Unknown building layer condition");
    }
    return layer;
}
BuildingSpriteLight buildingLight(const Json& j) {
    fields(j, {"position", "radius", "intensity", "color"}, {"flicker", "when"});
    BuildingSpriteLight light;
    light.position = pair(j.at("position"), -8192, 8192);
    light.radius = real(j.at("radius"), .1f, 16);
    light.intensity = real(j.at("intensity"), 0, 1);
    const auto& color = j.at("color");
    if (!color.is_array() || color.size() != 3) throw std::runtime_error("Light color requires RGB components");
    light.color = (number(color[0], 0, 255) << 16) | (number(color[1], 0, 255) << 8) | number(color[2], 0, 255);
    if (j.contains("flicker")) light.flicker = real(j.at("flicker"), 0, .25f);
    if (j.contains("when")) {
        const auto when = string(j.at("when"));
        if (when == "training") light.when = BuildingLayerWhen::Training;
        else if (when != "always") throw std::runtime_error("Unknown building light condition");
    }
    return light;
}
}
void parseBuildingSprite(EntityDefinition& e, const Json& sprite) {
    if (sprite.is_null()) return;
    fields(sprite, {"scale", "stages"});
    auto& s = e.buildingSprite;
    s.scale = real(sprite.at("scale"), .01f, 4);
    const auto& stages = sprite.at("stages");
    if (!stages.is_array() || stages.size() < 2 || stages.size() > 101)
        throw std::runtime_error("Building sprite requires construction and complete stages");
    int previous = -1;
    for (const auto& j : stages) {
        fields(j, {"from", "image", "teamMask", "source", "anchor"}, {"layers", "lights"});
        BuildingSpriteStage stage;
        stage.from = number(j.at("from"), 0, 100);
        if (stage.from <= previous) throw std::runtime_error("Building stages must have increasing percentages");
        previous = stage.from;
        stage.image = imagePath(j.at("image"));
        if (!j.at("teamMask").is_null()) stage.teamMask = imagePath(j.at("teamMask"));
        stage.source = sourceRectangle(j.at("source"));
        stage.anchor = pair(j.at("anchor"), 0, 1);
        if (j.contains("layers")) {
            const auto& layers = j.at("layers");
            if (!layers.is_array() || layers.size() > 32) throw std::runtime_error("Building stage supports up to 32 layers");
            for (const auto& layer : layers) stage.layers.push_back(buildingLayer(layer));
        }
        if (j.contains("lights")) {
            const auto& lights = j.at("lights");
            if (!lights.is_array() || lights.size() > 16) throw std::runtime_error("Building stage supports up to 16 lights");
            for (const auto& light : lights) stage.lights.push_back(buildingLight(light));
        }
        s.stages.push_back(std::move(stage));
    }
    if (s.stages.front().from != 0 || s.stages.back().from != 100)
        throw std::runtime_error("Building stages must start at 0 and end at 100 percent");
}
void parseWeapon(EntityDefinition& e, const Json& attack, const Json& sprite) {
    fields(attack, {"range", "windupTicks", "recoveryTicks", "cooldownTicks", "targets", "projectile"});
    e.attackRange = real(attack.at("range"), .1f, 32);
    e.attackWindupTicks = number(attack.at("windupTicks"), 0, 3600);
    e.attackRecoveryTicks = number(attack.at("recoveryTicks"), 0, 3600);
    e.attackCooldownTicks = number(attack.at("cooldownTicks"), 0, 3600);
    if (e.attackWindupTicks + e.attackRecoveryTicks + e.attackCooldownTicks == 0)
        throw std::runtime_error("Attack cycle cannot have zero duration");
    const auto targets = string(attack.at("targets"));
    if (targets == "same_layer") e.attackTargets = AttackTargets::SameLayer;
    else if (targets == "ground") e.attackTargets = AttackTargets::Ground;
    else if (targets == "air") e.attackTargets = AttackTargets::Air;
    else if (targets == "all") e.attackTargets = AttackTargets::All;
    else throw std::runtime_error("Unknown attack targets");
    const auto& projectile = attack.at("projectile");
    if (!projectile.is_null()) {
        fields(projectile, {"targeting", "image", "source", "size", "rotationOffset", "speed", "arcHeight", "launchHeight", "impactHeight", "splashRadius", "friendlyFire"});
        auto& p = e.projectile.emplace();
        const auto mode = string(projectile.at("targeting"));
        if (mode == "unit") p.targeting = ProjectileTargeting::Unit;
        else if (mode == "point") p.targeting = ProjectileTargeting::Point;
        else throw std::runtime_error("Unknown projectile targeting");
        p.image = imagePath(projectile.at("image"));
        const auto& source = projectile.at("source");
        if (!source.is_array() || source.size() != 4) throw std::runtime_error("Projectile source requires four components");
        for (size_t i = 0; i < 4; ++i) p.source[i] = number(source[i], 0, 8192);
        if ((p.source[2] == 0) != (p.source[3] == 0)) throw std::runtime_error("Invalid projectile source size");
        p.size = pair(projectile.at("size"), 1, 512);
        p.rotationOffset = real(projectile.at("rotationOffset"), -360, 360);
        p.speed = real(projectile.at("speed"), .1f, 100);
        p.arcHeight = real(projectile.at("arcHeight"), 0, 32);
        p.launchHeight = real(projectile.at("launchHeight"), 0, 8);
        p.impactHeight = real(projectile.at("impactHeight"), 0, 8);
        p.splashRadius = real(projectile.at("splashRadius"), 0, 16);
        p.friendlyFire = projectile.at("friendlyFire").get<bool>();
        if ((p.targeting == ProjectileTargeting::Point && p.splashRadius <= 0) ||
            (p.targeting == ProjectileTargeting::Unit && (p.splashRadius != 0 || p.friendlyFire)))
            throw std::runtime_error("Point projectiles require a splash radius; unit projectiles have single-target damage");
    }
    if (sprite.is_null()) return;
    fields(sprite, {"image", "frameSize", "size", "anchor", "rows", "idle", "walk", "windup", "recovery", "teamMask"});
    auto& s = e.sprite;
    s.image = imagePath(sprite.at("image"));
    const auto& frameSize = sprite.at("frameSize");
    if (!frameSize.is_array() || frameSize.size() != 2) throw std::runtime_error("Sprite frame needs width and height");
    s.frameWidth = number(frameSize[0], 1, 1024); s.frameHeight = number(frameSize[1], 1, 1024);
    s.size = pair(sprite.at("size"), 1, 1024); s.anchor = pair(sprite.at("anchor"), 0, 1);
    const auto& rows = sprite.at("rows");
    if (!rows.is_array() || rows.size() != 8) throw std::runtime_error("Sprite requires eight facing rows");
    for (size_t i = 0; i < 8; ++i) s.rows[i] = number(rows[i], 0, 255);
    s.idle = number(sprite.at("idle"), 0, 255);
    s.walk = frames(sprite.at("walk")); s.windup = frames(sprite.at("windup")); s.recovery = frames(sprite.at("recovery"));
    const auto mask = string(sprite.at("teamMask"));
    if (mask == "none") s.teamMask = SpriteTeamMask::None;
    else if (mask == "blue") s.teamMask = SpriteTeamMask::Blue;
    else if (mask == "purple") s.teamMask = SpriteTeamMask::Purple;
    else throw std::runtime_error("Unknown sprite team mask");
}
}
