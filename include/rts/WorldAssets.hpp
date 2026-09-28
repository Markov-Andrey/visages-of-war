#pragma once
#include "rts/Environment.hpp"
#include "rts/Paths.hpp"
#include <array>

namespace rts {
struct TerrainMaterial {
    std::string id, name;
    std::filesystem::path image;
    float repeatCells = 16;
    unsigned tint = 0xffffff;
};
struct WorldObjectDefinition {
    std::string id, name;
    bool gameplay{};
    EnvironmentKind kind = EnvironmentKind::Decoration;
    int width = 1, height = 1, health = 100;
    std::vector<bool> collision{false};
    bool blocksVision{};
    std::filesystem::path image; // Empty: built-in rock/arch placeholder geometry.
    std::array<float, 4> source{}; // Pixel rectangle; zero width/height means the whole image.
    Vec2 size{64, 64}, anchor{.5f, 1};
    bool pixelArt{};
};
class WorldAssets {
public:
    static WorldAssets load(const Paths& paths);
    const TerrainMaterial& material(const std::string& id) const;
    const WorldObjectDefinition& object(const std::string& id) const;
    const auto& materials() const { return materials_; }
    const auto& objects() const { return objects_; }
    EnvironmentObject instantiate(const std::string& id, EntityId instance, Cell origin) const;
private:
    std::vector<TerrainMaterial> materials_;
    std::vector<WorldObjectDefinition> objects_;
};
}
