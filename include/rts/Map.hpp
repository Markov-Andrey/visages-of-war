#pragma once

#include "rts/Types.hpp"
#include "rts/Environment.hpp"
#include "rts/Landscape.hpp"
#include <filesystem>
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace rts {
enum class Surface { Land, ShallowWater, DeepWater };
struct Tile {
    int height{};
    bool blocked{};
    Cell ramp{}; // Lower land/shallow-water tile toward adjacent land one level higher.
    Surface surface = Surface::Land; // Water depth is independent of terrain elevation.
};

class Map {
public:
    Map(int width, int height);
    int width() const { return width_; }
    int height() const { return height_; }
    bool contains(Cell c) const;
    bool walkable(Cell c, MovementType movement = MovementType::Walking) const;
    void occupy(Cell c);
    void release(Cell c);
    unsigned occupancy(Cell c) const;
    void clearOccupancy();
    // Sight is independent of walkability: crystals and buildings are not forests.
    void rebuildVisionBlockers(std::span<const EnvironmentObject> objects);
    bool blocksVision(Cell c) const;
    Tile& at(Cell c);
    const Tile& at(Cell c) const;
    bool canStep(Cell from, Cell to, MovementType movement = MovementType::Walking) const;
    bool canTraverse(Vec2 from, Vec2 to, float radius, MovementType movement = MovementType::Walking) const;
    // The same sloped surface is used by rendering, picking and unit placement.
    float surfaceHeight(Cell c, Vec2 world) const;
    std::array<Vec2, 4> surfaceCorners(Cell c, const WorldView& view) const;
    std::optional<Cell> pick(Vec2 screen, const WorldView& view) const;
    std::optional<Vec2> pickPosition(Vec2 screen, const WorldView& view) const;
private:
    bool cardinalStep(Cell from, Cell to, MovementType movement) const;
    int width_;
    int height_;
    std::vector<Tile> tiles_;
    std::vector<unsigned> occupancy_;
    std::vector<bool> visionBlockers_;
};

struct Crystal {
    static constexpr int defaultReserve = 1000;
    static constexpr PlayerId owner = neutralPlayer;
    Cell cell;
    int remaining = defaultReserve;
    EntityId id{}; // Assigned once by the match; depletion keeps this identity.
    std::string definitionId = "crystal.small";
    int width = 1, height = 1, capacity = defaultReserve; // Derived from the resource catalog.
    bool contains(Cell c) const { return c.x >= cell.x && c.y >= cell.y && c.x < cell.x + width && c.y < cell.y + height; }
    Vec2 center() const { return {cell.x + width * .5f, cell.y + height * .5f}; }
    float depth() const { return cell.y + height - .5f; }
};
struct UnitSpawn { std::string definitionId; PlayerId owner{}; Cell cell; };
struct Scenario {
    Map map;
    Cell hall;
    Cell worker;
    std::vector<Crystal> crystals;
    std::vector<EnvironmentObject> environment;
    int playerSlots = 1;
    std::vector<Cell> extraWorkers;
    int startingCrystals{};
    std::vector<UnitSpawn> units; // Includes scripted hostile forces, independent of playable slots.
    std::optional<Cell> heroSpawn; // Uses the selected commander's hero definition.
    Landscape landscape;
    std::string name = "Новая карта";
    Cell hallFootprint{}; // Derived reservation from the entity catalog; never serialized.
};
class WorldAssets;
Scenario loadScenario(const std::filesystem::path& path);
Scenario loadScenario(const std::filesystem::path& path, const WorldAssets& assets, Cell hallFootprint);
void saveScenario(const Scenario& scenario, const std::filesystem::path& path);
// Validate individual cosmetic edits without rebuilding navigation or vision.
void validatePaintStamp(const Map& map, const PaintStamp& stamp);
void validateDecoration(const Map& map, const Decoration& decoration);
// Rebuild derived occupancy after loading/editing; no state from a running match is serialized.
void rebuildScenario(Scenario& scenario, Cell hallFootprint = {});
// Reserve/rebind only the starting depot, keeping all other map occupancy intact.
void reserveScenarioHall(Scenario& scenario, Cell footprint);
}
