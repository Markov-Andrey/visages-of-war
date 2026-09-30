#pragma once
#include "rts/Definitions.hpp"
#include "rts/Menu.hpp"

namespace rts {
inline std::vector<const EntityDefinition*> libraryEntries(const Definitions& definitions, const std::string& faction) {
    std::vector<const EntityDefinition*> entries;
    for (const auto& entity : definitions.entities())
        if (entity.libraryVisible && entity.factionId == faction) entries.push_back(&entity);
    return entries;
}
inline std::vector<const FactionDefinition*> libraryFactions(const Definitions& definitions) {
    std::vector<const FactionDefinition*> factions;
    for (const auto& faction : definitions.factions())
        if (!libraryEntries(definitions, faction.id).empty()) factions.push_back(&faction);
    return factions;
}
// Book coordinates are shared by drawing and input, including small/high-DPI windows.
struct LibraryLayout {
    static constexpr size_t factionRows = 2, entryRows = 7;
    float scale;
    Vec2 origin;
    UiRect back{34, 736, 224, 42}, races{34, 170, 224, 144}, entries{34, 360, 224, 308};
    UiRect color{968, 42, 280, 42};
    explicit LibraryLayout(Vec2 size) : scale(std::max(.01f, std::min(size.x / 1280, size.y / 800))),
        origin{(size.x - 1280 * scale) * .5f, (size.y - 800 * scale) * .5f} {}
    Vec2 local(Vec2 point) const { return (point - origin) * (1 / scale); }
    UiRect factionRow(size_t index) const { return {races.x, races.y + index * 72.0f, races.width, 66}; }
    UiRect entryRow(size_t index) const { return {entries.x, entries.y + index * 44.0f, entries.width, 40}; }
};
}
