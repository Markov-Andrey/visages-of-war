#include "rts/Map.hpp"
#include "rts/WorldAssets.hpp"
#include "rts/Definitions.hpp"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <fstream>
#include <iomanip>
#include <set>
#include <stdexcept>

namespace rts {
namespace {
using Json = nlohmann::json;
Cell cell(const Json& j) { return {j.at(0).get<int>(), j.at(1).get<int>()}; }
Vec2 position(const Json& j) { return {j.at(0).get<float>(), j.at(1).get<float>()}; }
Json xy(Cell c) { return Json::array({c.x, c.y}); }
Json xy(Vec2 p) { return Json::array({p.x, p.y}); }
bool inside(const Map& m, Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.y >= 0 && p.x < m.width() && p.y < m.height(); }
bool range(float v, float a, float b) { return std::isfinite(v) && v >= a && v <= b; }
void occupyFlat(Map& m, Cell origin, int width, int height, const std::vector<bool>& mask) {
    if (width < 1 || height < 1 || width > m.width() || height > m.height() || !m.contains(origin))
        throw std::runtime_error("Invalid object footprint");
    const int level = m.at(origin).height;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const Cell c = origin + Cell{x,y};
        if (!m.contains(c) || m.at(c).height != level || m.at(c).surface != Surface::Land || m.at(c).ramp != Cell{} || m.at(c).blocked)
            throw std::runtime_error("Object footprint requires flat land");
        if (mask[static_cast<size_t>(y)*width+x]) {
            if (m.occupancy(c)) throw std::runtime_error("Overlapping map objects");
            m.occupy(c);
        }
    }
}
void occupyHall(Map& m, Cell origin, Cell footprint) {
    if (footprint.x < 1 || footprint.y < 1 || footprint.x > m.width() || footprint.y > m.height())
        throw std::runtime_error("Invalid starting depot footprint");
    occupyFlat(m, origin, footprint.x, footprint.y, std::vector<bool>(footprint.x * footprint.y, true));
}
}
void reserveScenarioHall(Scenario& s, Cell footprint) {
    auto map = s.map;
    for (int y = 0; y < s.hallFootprint.y; ++y) for (int x = 0; x < s.hallFootprint.x; ++x)
        map.release(s.hall + Cell{x,y});
    occupyHall(map, s.hall, footprint);
    s.map = std::move(map); s.hallFootprint = footprint;
}
void rebuildScenario(Scenario& s, Cell hallFootprint) {
    if (hallFootprint != Cell{}) s.hallFootprint = hallFootprint;
    auto& m = s.map;
    m.clearOccupancy();
    if (s.playerSlots != 1 || s.startingCrystals < 0 || s.startingCrystals > 1000000) throw std::runtime_error("Invalid map player settings");
    for (int y = 0; y < m.height(); ++y) for (int x = 0; x < m.width(); ++x) {
        const Cell c{x,y}; const auto& t = m.at(c);
        if (t.height < -1 || t.height > 3) throw std::runtime_error("Terrain height must be in [-1, 3]");
        if (t.ramp != Cell{} && (std::abs(t.ramp.x) + std::abs(t.ramp.y) != 1 || !m.contains(c+t.ramp) ||
            t.surface != Surface::Land || t.blocked || m.at(c+t.ramp).surface != Surface::Land ||
            m.at(c+t.ramp).blocked || m.at(c+t.ramp).height != t.height+1)) throw std::runtime_error("Invalid ramp: expected a lower land tile pointing uphill");
    }
    occupyHall(m, s.hall, s.hallFootprint);
    std::set<EntityId> ids;
    for (const auto& o : s.environment) {
        if (o.id == 0 || o.id >= 100000000 || !ids.insert(o.id).second || o.hitPoints < 0 || o.hitPoints > o.maximumHitPoints)
            throw std::runtime_error("Invalid environment identity or health");
        // Inactive objects keep their placement and ID, without reserving the footprint.
        if (o.active()) occupyFlat(m, o.origin, o.width, o.height, o.collision);
        else if (!m.contains(o.origin) || !m.contains(o.origin + Cell{o.width-1,o.height-1})) throw std::runtime_error("Object outside map");
    }
    for (const auto& c : s.crystals) {
        if (c.remaining < 1 || c.remaining > Crystal::maximum) throw std::runtime_error("Invalid crystal reserve");
        occupyFlat(m, c.cell, 1, 1, {true});
    }
    std::set<std::pair<int,int>> workers;
    for (const auto c : [&] { auto cells=s.extraWorkers; cells.push_back(s.worker); return cells; }())
        if (!m.walkable(c, MovementType::Amphibious) || !workers.insert({c.x,c.y}).second) throw std::runtime_error("Invalid worker spawn");
    if (s.heroSpawn && !m.contains(*s.heroSpawn)) throw std::runtime_error("Hero spawn outside map");
    for (const auto& u : s.units) if (!m.contains(u.cell) || u.owner >= neutralPlayer || u.definitionId.empty()) throw std::runtime_error("Invalid unit spawn");
    for (const auto& d : s.landscape.decorations)
        if (d.id == 0 || d.id >= 100000000 || !ids.insert(d.id).second || !inside(m,d.position) || !range(d.scale,.1f,8) || !range(d.rotation,-360,360))
            throw std::runtime_error("Invalid decoration placement");
    if (s.landscape.paint.size() > 200000) throw std::runtime_error("Too many paint stamps");
    for (const auto& p : s.landscape.paint)
        if (!inside(m,p.position) || !range(p.radius,.1f,16) || !range(p.opacity,.01f,1) || !range(p.hardness,0,1)) throw std::runtime_error("Invalid paint brush data");
    m.rebuildVisionBlockers(s.environment);
}
Scenario loadScenario(const std::filesystem::path& path) {
    const auto paths = Paths::discover();
    const auto definitions = Definitions::load(paths.asset(L"data/catalog.json"));
    const auto& depot = definitions.startingDepot(definitions.commanders().front().factionId);
    return loadScenario(path, WorldAssets::load(paths), {depot.width, depot.height});
}
Scenario loadScenario(const std::filesystem::path& path, const WorldAssets& assets, Cell hallFootprint) {
    if (std::filesystem::file_size(path) > 64*1024*1024) throw std::runtime_error("Map file is too large");
    std::ifstream input(path); if (!input) throw std::runtime_error("Cannot open map");
    const auto j = Json::parse(input);
    if (j.at("format") != "rts-world") throw std::runtime_error("Expected rts-world map");
    const auto size = cell(j.at("size"));
    Scenario s{Map(size.x,size.y), cell(j.at("start").at("hall")), {}, {}};
    s.name = j.at("name").get<std::string>();
    s.startingCrystals = j.at("start").at("crystals").get<int>();
    const auto& workers = j.at("start").at("workers");
    if (workers.empty()) throw std::runtime_error("Map requires a starting worker");
    s.worker = cell(workers[0]);
    for (size_t i=1;i<workers.size();++i) s.extraWorkers.push_back(cell(workers[i]));
    if (!j.at("start").at("hero").is_null()) s.heroSpawn = cell(j.at("start").at("hero"));
    const auto& terrain = j.at("terrain");
    s.landscape.baseMaterial = terrain.at("base").get<std::string>(); assets.material(s.landscape.baseMaterial);
    if (terrain.at("heights").size() != static_cast<size_t>(size.y) || terrain.at("surfaces").size() != static_cast<size_t>(size.y) || terrain.at("blocked").size() != static_cast<size_t>(size.y)) throw std::runtime_error("Invalid terrain rows");
    for (int y=0;y<size.y;++y) {
        const auto heights = terrain.at("heights").at(y).get<std::string>(), surfaces = terrain.at("surfaces").at(y).get<std::string>();
        const auto blocked = terrain.at("blocked").at(y).get<std::string>();
        if (heights.size()!=static_cast<size_t>(size.x) || surfaces.size()!=heights.size() || blocked.size()!=heights.size()) throw std::runtime_error("Invalid terrain row width");
        for (int x=0;x<size.x;++x) {
            const char h=heights[x], surface=surfaces[x];
            if ((h!='-' && (h<'0'||h>'3')) || (surface!='L'&&surface!='S'&&surface!='D') || (blocked[x]!='0'&&blocked[x]!='1')) throw std::runtime_error("Invalid terrain cell");
            s.map.at({x,y}) = {h=='-' ? -1 : h-'0', blocked[x]=='1', {}, surface=='S' ? Surface::ShallowWater : surface=='D' ? Surface::DeepWater : Surface::Land};
        }
    }
    for (const auto& r : terrain.at("ramps")) {
        const Cell c=cell(r); if (!s.map.contains(c) || s.map.at(c).ramp!=Cell{}) throw std::runtime_error("Invalid or duplicate ramp");
        s.map.at(c).ramp={r.at(2).get<int>(),r.at(3).get<int>()};
    }
    for (const auto& p : j.at("paint")) {
        PaintStamp stamp{p.at("material").get<std::string>(),position(p.at("position")),p.at("radius").get<float>(),p.at("opacity").get<float>(),p.at("hardness").get<float>(),p.at("erase").get<bool>()};
        assets.material(stamp.material); s.landscape.paint.push_back(std::move(stamp));
    }
    for (const auto& e : j.at("environment")) {
        auto o=assets.instantiate(e.at("asset").get<std::string>(),e.at("id").get<EntityId>(),cell(e.at("cell")));
        const int health=e.at("health").get<int>(); if(health<0) throw std::runtime_error("Negative environment health");
        o.hitPoints=std::min(health,o.maximumHitPoints); s.environment.push_back(std::move(o));
    }
    for (const auto& e : j.at("decorations")) {
        Decoration d{e.at("id").get<EntityId>(),e.at("asset").get<std::string>(),position(e.at("position")),e.at("scale").get<float>(),e.at("rotation").get<float>()};
        if (assets.object(d.definitionId).gameplay) throw std::runtime_error("Gameplay asset used as cosmetic decoration");
        s.landscape.decorations.push_back(std::move(d));
    }
    for (const auto& c : j.at("resources")) s.crystals.push_back({cell(c.at("cell")),c.at("remaining").get<int>()});
    for (const auto& u : j.at("units")) {
        const int owner=u.at("owner").get<int>(); if (owner<0 || owner>=neutralPlayer) throw std::runtime_error("Invalid unit owner");
        s.units.push_back({u.at("asset").get<std::string>(),static_cast<PlayerId>(owner),cell(u.at("cell"))});
    }
    rebuildScenario(s, hallFootprint); return s;
}
void saveScenario(const Scenario& s, const std::filesystem::path& path) {
    auto checked=s; rebuildScenario(checked);
    Json j{{"format","rts-world"},{"name",s.name},{"size",Json::array({s.map.width(),s.map.height()})}};
    auto& start=j["start"]; start["hall"]=xy(s.hall); start["crystals"]=s.startingCrystals;
    start["workers"]=Json::array({xy(s.worker)}); for(auto c:s.extraWorkers) start["workers"].push_back(xy(c));
    start["hero"]=s.heroSpawn ? xy(*s.heroSpawn) : Json(nullptr);
    auto& terrain=j["terrain"]; terrain["base"]=s.landscape.baseMaterial;
    terrain["heights"]=Json::array(); terrain["surfaces"]=Json::array(); terrain["blocked"]=Json::array(); terrain["ramps"]=Json::array();
    for(int y=0;y<s.map.height();++y) {
        std::string heights,surfaces,blocked;
        for(int x=0;x<s.map.width();++x) {
            const auto& t=s.map.at({x,y}); heights+=t.height==-1?'-':static_cast<char>('0'+t.height); blocked+=t.blocked?'1':'0';
            surfaces+=t.surface==Surface::Land?'L':t.surface==Surface::ShallowWater?'S':'D';
            if(t.ramp!=Cell{}) terrain["ramps"].push_back(Json::array({x,y,t.ramp.x,t.ramp.y}));
        }
        terrain["heights"].push_back(heights); terrain["surfaces"].push_back(surfaces); terrain["blocked"].push_back(blocked);
    }
    for(auto key:{"paint","environment","decorations","resources","units"}) j[key]=Json::array();
    for(const auto& p:s.landscape.paint) j["paint"].push_back({{"material",p.material},{"position",xy(p.position)},{"radius",p.radius},{"opacity",p.opacity},{"hardness",p.hardness},{"erase",p.erase}});
    for(const auto& o:s.environment) j["environment"].push_back({{"id",o.id},{"asset",o.definitionId.empty()?(o.kind==EnvironmentKind::Tree?"tree":o.kind==EnvironmentKind::Rock?"rock":"arch"):o.definitionId},{"cell",xy(o.origin)},{"health",o.hitPoints}});
    for(const auto& d:s.landscape.decorations) j["decorations"].push_back({{"id",d.id},{"asset",d.definitionId},{"position",xy(d.position)},{"scale",d.scale},{"rotation",d.rotation}});
    for(const auto& c:s.crystals) j["resources"].push_back({{"cell",xy(c.cell)},{"remaining",c.remaining}});
    for(const auto& u:s.units) j["units"].push_back({{"asset",u.definitionId},{"owner",u.owner},{"cell",xy(u.cell)}});
    auto temporary=path; temporary+=L".writing";
    { std::ofstream output(temporary,std::ios::binary|std::ios::trunc); output<<std::setw(2)<<j<<'\n'; output.flush(); if(!output) throw std::runtime_error("Cannot write map; original file preserved"); }
    if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace map; original file preserved");
}
}
