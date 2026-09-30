#include "GameApplication.hpp"
#include "rts/Library.hpp"

namespace rts::game {
void GameApplication::advanceMenu(float elapsed) {
    if (menu_.page == MenuPage::Library) menu_.librarySeconds += elapsed;
}
bool GameApplication::libraryLinkAt(Vec2 mouse) const {
    const LibraryLayout layout(renderer_.size());
    const auto point = layout.local(mouse);
    const ColorSelectLayout colors(layout.color, 800);
    if (menu_.colorDropdown) return colors.field.contains(point) || colors.pick(point).has_value();
    if (colors.field.contains(point)) return true;
    if (layout.back.contains(point)) return true;
    const auto factions = libraryFactions(definitions_);
    if (factions.empty()) return false;
    for (size_t i = 0; i < LibraryLayout::factionRows && menu_.libraryFactionScroll + i < factions.size(); ++i)
        if (layout.factionRow(i).contains(point)) return true;
    const auto entries = libraryEntries(definitions_, factions.at(menu_.libraryFaction)->id);
    for (size_t i = 0; i < LibraryLayout::entryRows && menu_.libraryEntryScroll + i < entries.size(); ++i)
        if (layout.entryRow(i).contains(point)) return true;
    return false;
}
void GameApplication::libraryClick() {
    const LibraryLayout layout(renderer_.size());
    const auto point = layout.local(mouse_);
    if (colorSelectClick(ColorSelectLayout(layout.color, 800), point)) return;
    if (layout.back.contains(point)) { menu_.page = MenuPage::Main; return; }
    const auto factions = libraryFactions(definitions_);
    if (factions.empty()) return;
    for (size_t i = 0; i < LibraryLayout::factionRows && menu_.libraryFactionScroll + i < factions.size(); ++i)
        if (layout.factionRow(i).contains(point)) {
            menu_.libraryFaction = menu_.libraryFactionScroll + i;
            menu_.libraryEntry = menu_.libraryEntryScroll = 0;
            menu_.librarySeconds = 0;
            return;
        }
    const auto entries = libraryEntries(definitions_, factions.at(menu_.libraryFaction)->id);
    for (size_t i = 0; i < LibraryLayout::entryRows && menu_.libraryEntryScroll + i < entries.size(); ++i)
        if (layout.entryRow(i).contains(point)) {
            menu_.libraryEntry = menu_.libraryEntryScroll + i;
            menu_.librarySeconds = 0;
            return;
        }
}
void GameApplication::libraryScroll(int direction) {
    if (menu_.colorDropdown) return;
    const LibraryLayout layout(renderer_.size());
    const auto point = layout.local(mouse_);
    const auto factions = libraryFactions(definitions_);
    if (factions.empty()) return;
    const auto scroll = [&](size_t& offset, size_t count, size_t rows) {
        offset = size_t(std::clamp(int(offset) + direction, 0, std::max(0, int(count) - int(rows))));
    };
    if (layout.races.contains(point)) scroll(menu_.libraryFactionScroll, factions.size(), LibraryLayout::factionRows);
    if (layout.entries.contains(point))
        scroll(menu_.libraryEntryScroll, libraryEntries(definitions_, factions.at(menu_.libraryFaction)->id).size(), LibraryLayout::entryRows);
}
}
