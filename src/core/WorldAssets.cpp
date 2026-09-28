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
    std::set<std::string> ids;
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
