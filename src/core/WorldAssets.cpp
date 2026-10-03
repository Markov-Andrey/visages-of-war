#include "rts/WorldAssets.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace rts {
namespace {
using Json = nlohmann::json;
Json read(const std::filesystem::path& path) {
    std::ifstream input(path); if (!input) throw std::runtime_error("Cannot open world asset catalog");
    return Json::parse(input);
}
std::filesystem::path utf8Path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
float number(const Json& j, float minimum, float maximum) {
    const float v = j.get<float>();
    if (!std::isfinite(v) || v < minimum || v > maximum) throw std::runtime_error("World asset parameter out of range");
    return v;
}
std::string identifier(const Json& j, std::set<std::string>& used) {
    const auto s = j.get<std::string>();
    if (s.empty() || s.size() > 100 || !used.insert(s).second) throw std::runtime_error("Empty or duplicate world asset ID: " + s);
    return s;
}
}
WorldAssets WorldAssets::load(const Paths& paths) {
    WorldAssets result;
    const auto index = read(paths.asset(L"world/catalog.json"));
    const auto remains = read(paths.asset(utf8Path(index.at("remainsFile").get<std::string>())));
    const auto& bones = remains.at("bones");
    auto& sprite = result.bonesSprite_;
    sprite.image = utf8Path(bones.at("image").get<std::string>()); paths.asset(sprite.image);
    sprite.scale = number(bones.at("scale"), .01f, 4);
    sprite.pixelArt = bones.value("pixelArt", false);
    const auto& variants = bones.at("variants");
    if (!variants.is_array() || variants.empty() || variants.size() > 256) throw std::runtime_error("Invalid bones variants");
    for (const auto& variant : variants) {
        const auto& source = variant.at("source");
        const auto& anchor = variant.at("anchor");
        if (!source.is_array() || source.size() != 4 || !anchor.is_array() || anchor.size() != 2)
            throw std::runtime_error("Invalid bones crop or anchor");
        UnitDeathFrame frame;
        for (size_t i = 0; i < 4; ++i) {
            if (!source[i].is_number_integer()) throw std::runtime_error("Bones crop must use integer pixels");
            frame.source[i] = int(number(source[i], i < 2 ? 0.0f : 1.0f, 8192));
        }
        frame.anchor = {number(anchor[0], 0, float(frame.source[2])), number(anchor[1], 0, float(frame.source[3]))};
        sprite.variants.push_back(frame);
    }
    std::set<std::string> ids;
    const auto resources = read(paths.asset(utf8Path(index.at("resourcesFile").get<std::string>())));
    for (const auto& [id, image] : resources.at("icons").items()) {
        auto path = utf8Path(image.get<std::string>()); paths.asset(path);
        result.resourceIcons_.emplace(id, std::move(path));
    }
    for (const auto* id : {"crystal", "supply"}) if (!result.resourceIcons_.contains(id))
        throw std::runtime_error("Missing resource icon: " + std::string(id));
    const auto& crystal = resources.at("crystal");
    auto& resource = result.crystalSprite_;
    resource.image = utf8Path(crystal.at("image").get<std::string>()); paths.asset(resource.image);
    resource.scale = number(crystal.at("scale"), .01f, 4);
    const auto color = crystal.at("glowColor").get<std::string>();
    if (color.size() != 6 || color.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        throw std::runtime_error("Invalid crystal glow color");
    resource.glowColor = static_cast<unsigned>(std::stoul(color, nullptr, 16));
    const auto& crystalVariants = crystal.at("variants");
    if (!crystalVariants.is_array() || crystalVariants.empty() || crystalVariants.size() > 256)
        throw std::runtime_error("Invalid crystal variants");
    for (const auto& variant : crystalVariants) {
        const auto& source = variant.at("source"); const auto& anchor = variant.at("anchor");
        if (!source.is_array() || source.size() != 4 || !anchor.is_array() || anchor.size() != 2)
            throw std::runtime_error("Invalid crystal crop or anchor");
        UnitDeathFrame frame;
        for (size_t i = 0; i < 4; ++i) {
            if (!source[i].is_number_integer()) throw std::runtime_error("Crystal crop must use integer pixels");
            frame.source[i] = int(number(source[i], i < 2 ? 0.f : 1.f, 8192));
        }
        frame.anchor = {number(anchor[0], 0, float(frame.source[2])), number(anchor[1], 0, float(frame.source[3]))};
        resource.variants.push_back(frame);
    }
    for (const auto& file : index.at("materialFiles")) for (const auto& j : read(paths.asset(utf8Path(file.get<std::string>())))) {
        TerrainMaterial m;
        m.id = identifier(j.at("id"), ids); m.name = j.at("name").get<std::string>();
        m.image = utf8Path(j.at("image").get<std::string>()); paths.asset(m.image);
        m.repeatCells = number(j.at("repeatCells"), .25f, 128);
        const auto tint = j.value("tint", std::string("ffffff"));
        if (tint.size() != 6 || tint.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) throw std::runtime_error("Invalid texture tint");
        m.tint = static_cast<unsigned>(std::stoul(tint, nullptr, 16));
        result.materials_.push_back(std::move(m));
    }
    ids.clear();
    for (const auto& file : index.at("objectFiles")) for (const auto& j : read(paths.asset(utf8Path(file.get<std::string>())))) {
        WorldObjectDefinition d;
        d.id = identifier(j.at("id"), ids); d.name = j.at("name").get<std::string>();
        d.gameplay = j.at("gameplay").get<bool>();
        const auto kind = j.at("kind").get<std::string>();
        if (kind == "tree") d.kind = EnvironmentKind::Tree;
        else if (kind == "rock") d.kind = EnvironmentKind::Rock;
        else if (kind == "arch") d.kind = EnvironmentKind::Arch;
        else if (kind != "decoration") throw std::runtime_error("Unknown world object kind: " + kind);
        d.width = j.at("footprint").at(0).get<int>(); d.height = j.at("footprint").at(1).get<int>();
        if (d.width < 1 || d.height < 1 || d.width > 16 || d.height > 16) throw std::runtime_error("Invalid world object footprint");
        d.collision = j.at("collision").get<std::vector<bool>>();
        if (d.collision.size() != static_cast<size_t>(d.width * d.height)) throw std::runtime_error("Invalid object collision mask");
        d.health = j.at("health").get<int>(); d.blocksVision = j.at("blocksVision").get<bool>();
        if (d.health < 1 || d.health > 1000000) throw std::runtime_error("Invalid object health");
        if (!d.gameplay && (d.blocksVision || std::any_of(d.collision.begin(), d.collision.end(), [](bool b) { return b; })))
            throw std::runtime_error("Cosmetic objects must not affect gameplay");
        const auto& s = j.at("sprite");
        const auto image = s.at("image").get<std::string>();
        if (!image.empty()) { d.image = utf8Path(image); paths.asset(d.image); }
        else if (d.kind != EnvironmentKind::Rock && d.kind != EnvironmentKind::Arch) throw std::runtime_error("Object requires a sprite");
        for (size_t i = 0; i < 4; ++i) d.source[i] = number(s.at("source").at(i), 0, 8192);
        d.size = {number(s.at("size").at(0), 1, 2048), number(s.at("size").at(1), 1, 2048)};
        d.anchor = {number(s.at("anchor").at(0), 0, 1), number(s.at("anchor").at(1), 0, 1)};
        d.pixelArt = s.value("pixelArt", false);
        result.objects_.push_back(std::move(d));
    }
    if (result.materials_.empty() || result.objects_.empty()) throw std::runtime_error("Empty world asset catalog");
    return result;
}
const TerrainMaterial& WorldAssets::material(const std::string& id) const {
    for (const auto& d : materials_) if (d.id == id) return d;
    throw std::runtime_error("Unknown terrain material: " + id);
}
const WorldObjectDefinition& WorldAssets::object(const std::string& id) const {
    for (const auto& d : objects_) if (d.id == id) return d;
    throw std::runtime_error("Unknown world object: " + id);
}
EnvironmentObject WorldAssets::instantiate(const std::string& id, EntityId instance, Cell origin) const {
    const auto& d = object(id);
    if (!d.gameplay) throw std::runtime_error("Cosmetic definition used as gameplay object");
    return {instance, d.kind, origin, d.width, d.height, d.collision, Interaction::Destructible, d.health, d.health, d.blocksVision, id};
}
}
