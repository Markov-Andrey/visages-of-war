#include "RenderSupport.hpp"
#include "rts/Library.hpp"

namespace rts {
using namespace render;
namespace {
struct DetailLine {
    LibraryDetail detail;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> label, value;
    float y{}, height{}, valueX{};
};
struct DetailPage { std::vector<DetailLine> lines; float height{}; };
DetailPage detailPage(IDWriteFactory* factory, IDWriteTextFormat* body, IDWriteTextFormat* title,
    const Definitions& definitions, const EntityDefinition& entity) {
    DetailPage page;
    const float width = LibraryLayout({1280, 800}).details.width - 12;
    const auto measure = [&](const std::string& value, float extent, bool heading,
        Microsoft::WRL::ComPtr<IDWriteTextLayout>& output) {
        const auto text = wide(value);
        check(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), heading ? title : body,
            extent, 1000000, output.GetAddressOf()));
        check(output->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING));
        check(output->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
        check(output->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
        DWRITE_TEXT_METRICS metrics{};
        check(output->GetMetrics(&metrics));
        return std::ceil(metrics.height);
    };
    for (auto detail : libraryDetails(definitions, entity)) {
        DetailLine line;
        line.detail = std::move(detail);
        const bool stat = line.detail.kind == LibraryDetailKind::Stat;
        if (line.detail.kind == LibraryDetailKind::Heading) page.height += 10;
        line.y = page.height;
        line.height = measure(line.detail.label, stat ? 186 : width, line.detail.kind == LibraryDetailKind::Title, line.label);
        if (stat) {
            line.valueX = 198 + (line.detail.icon.empty() ? 0.0f : 28.0f);
            line.height = std::max(line.height, measure(line.detail.value, width - line.valueX, false, line.value));
            if (!line.detail.icon.empty()) line.height = std::max(line.height, 24.0f);
        }
        page.height += line.height + 10;
        page.lines.push_back(std::move(line));
    }
    return page;
}
}
float Renderer::libraryDetailsHeight(const Definitions& definitions, const EntityDefinition& entity) const {
    return detailPage(writeFactory_.Get(), bodyFormat_.Get(), titleFormat_.Get(), definitions, entity).height;
}
void Renderer::drawLibrary(const MenuState& menu, const Definitions& definitions, Vec2 mouse) {
    // Browsing can introduce graphics from races outside the current match.
    residentMatchAssets_.reset();
    const LibraryLayout layout(size());
    mouse = layout.local(mouse);
    D2D1_MATRIX_3X2_F previous;
    target_->GetTransform(&previous);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(layout.scale, layout.scale) *
        D2D1::Matrix3x2F::Translation(layout.origin.x, layout.origin.y) * previous);
    worldOpacity_ = 1;
    // Keep only the current preview tint instead of retaining every full-size color variant.
    const auto color = teamRgb(menu.player.color);
    std::erase_if(maskedImages_, [color, this](const auto& entry) {
        const bool remove = !std::get<1>(entry.first).empty() && std::get<2>(entry.first) != color;
        if (remove) spriteLights_.erase(entry.second.Get());
        return remove;
    });
    const auto fill = [&](UiRect area, unsigned color, float opacity = 1) {
        brush_->SetColor(D2D1::ColorF(color, opacity));
        target_->FillRectangle(rect(area.x, area.y, area.width, area.height), brush_.Get());
    };
    const auto choice = [&](UiRect area, const std::wstring& caption, bool selected, const EntityDefinition* entry = nullptr) {
        fill(area, selected ? 0x514432 : (area.contains(mouse) ? 0x343836 : 0x1c2729), .97f);
        if (selected) fill({area.x, area.y, 3, area.height}, 0xd6b476);
        if (entry) {
            const auto& icon = entry->constructible ? entry->buildingSprite.icon : entry->sprite.icon;
            const auto& mask = entry->constructible ? entry->buildingSprite.iconMask : entry->sprite.iconMask;
            const UiRect iconBounds{area.x + 8, area.y + 4, 32, 32};
            if (!icon.empty()) uiImage(icon, mask, iconBounds, color);
            else centeredText(L"?", iconBounds, 0xbac5bb);
            const auto alignment = bodyFormat_->GetParagraphAlignment();
            bodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            text(caption, rect(area.x + 48, area.y + 2, area.width - 60, area.height - 4), selected ? 0xf2ddb5 : 0xbac5bb);
            bodyFormat_->SetParagraphAlignment(alignment);
            return;
        }
        text(caption, rect(area.x + 14, area.y + 10, area.width - 28, area.height - 14), selected ? 0xf2ddb5 : 0xbac5bb);
    };
    text(L"ЛИБРАРИЙ", rect(34, 28, 550, 40), 0xf1ddb5, true);
    text(L"Книга народов, воинов и святилищ", rect(36, 70, 650, 24), 0xb4b8aa);
    text(L"Цвет команды", rect(824, 52, 140, 24), 0xb4b8aa);
    line({34, 108}, {258, 108}, 0x806d4e);
    text(L"РАСЫ", rect(48, 139, 200, 24), 0xc5b088);
    text(L"ЗАПИСИ", rect(48, 329, 200, 24), 0xc5b088);
    choice(layout.back, L"‹  Назад   /   Esc", false);

    const auto factions = libraryFactions(definitions);
    for (size_t i = 0; i < LibraryLayout::factionRows && menu.libraryFactionScroll + i < factions.size(); ++i)
        choice(layout.factionRow(i), wide(factions[menu.libraryFactionScroll + i]->displayName), menu.libraryFaction == menu.libraryFactionScroll + i);
    fill({288, 124, 968, 602}, 0x000000, .4f);
    fill({278, 110, 970, 610}, 0x513526);
    fill({285, 114, 956, 602}, 0xa98c59);
    fill({292, 120, 464, 588}, 0xe6d9b7);
    fill({756, 120, 484, 588}, 0xf1e5c9);
    // Narrow translucent bands suggest the binding without introducing another art asset.
    for (int i = 0; i < 12; ++i) {
        fill({744.0f + i, 120, 1, 588}, 0x4a3225, .025f * i);
        fill({756.0f + i, 120, 1, 588}, 0x4a3225, .025f * (12 - i));
    }
    line({314, 162}, {724, 162}, 0xb6a078);
    line({782, 162}, {1216, 162}, 0xb6a078);
    line({314, 665}, {724, 665}, 0xb6a078);
    line({782, 665}, {1216, 665}, 0xb6a078);
    if (factions.empty()) {
        text(L"Книга пока пуста", rect(360, 260, 800, 44), 0x574732, true);
        drawColorSelect(ColorSelectLayout(layout.color, 800), menu, mouse);
        target_->SetTransform(previous);
        return;
    }
    const auto& faction = *factions.at(std::min(menu.libraryFaction, factions.size() - 1));
    for (size_t i = 0; i < libraryTabs.size(); ++i) {
        const auto count = libraryEntries(definitions, faction.id, libraryTabs[i]).size();
        choice(layout.tab(i), std::wstring(libraryTabNames[i]) + L" (" + std::to_wstring(count) + L")", menu.libraryTab == libraryTabs[i]);
    }
    const auto entries = libraryEntries(definitions, faction.id, menu.libraryTab);
    if (entries.empty()) {
        text(L"В этом разделе пока нет записей", rect(360, 260, 800, 44), 0x574732, true);
        drawColorSelect(ColorSelectLayout(layout.color, 800), menu, mouse);
        target_->SetTransform(previous);
        return;
    }
    const auto entryIndex = std::min(menu.libraryEntry, entries.size() - 1);
    const auto& entity = *entries[entryIndex];
    for (size_t i = 0; i < LibraryLayout::entryRows && menu.libraryEntryScroll + i < entries.size(); ++i) {
        const auto* entry = entries[menu.libraryEntryScroll + i];
        choice(layout.entryRow(i), wide(entry->displayName), entryIndex == menu.libraryEntryScroll + i, entry);
    }
    if (entries.size() > LibraryLayout::entryRows || factions.size() > LibraryLayout::factionRows)
        text(L"Колесо мыши — листать список", rect(42, 680, 224, 35), 0xb4b8aa);

    const UiRect art{320, 180, 396, 446};
    target_->PushAxisAlignedClip(rect(art.x, art.y, art.width, art.height), D2D1_ANTIALIAS_MODE_ALIASED);
    const auto& portrait = entity.constructible ? entity.buildingSprite.portrait : entity.sprite.portrait;
    const auto& portraitMask = entity.constructible ? entity.buildingSprite.portraitMask : entity.sprite.portraitMask;
    if (!portrait.empty()) {
        uiImage(portrait, portraitMask, art, color);
    } else {
        centeredText(L"Портрет готовится", art, 0x6f583b);
    }
    target_->PopAxisAlignedClip();

    const auto page = detailPage(writeFactory_.Get(), bodyFormat_.Get(), titleFormat_.Get(), definitions, entity);
    const auto area = layout.details;
    const float maximum = std::max(0.0f, page.height - area.height);
    const float offset = std::clamp(menu.libraryDetailScroll, 0.0f, maximum);
    target_->PushAxisAlignedClip(rect(area.x, area.y, area.width, area.height), D2D1_ANTIALIAS_MODE_ALIASED);
    for (const auto& row : page.lines) {
        const float y = area.y + row.y - offset;
        if (y + row.height < area.y || y >= area.y + area.height) continue;
        const bool stat = row.detail.kind == LibraryDetailKind::Stat;
        brush_->SetColor(D2D1::ColorF(stat ? 0x76634a : 0x3b3026));
        target_->DrawTextLayout({area.x, y}, row.label.Get(), brush_.Get());
        if (stat) {
            if (!row.detail.icon.empty()) drawResourceIcon(row.detail.icon, {area.x + 198, y, 24, 24});
            brush_->SetColor(D2D1::ColorF(0x392f25));
            target_->DrawTextLayout({area.x + row.valueX, y}, row.value.Get(), brush_.Get());
            line({area.x, y + row.height + 4}, {area.x + area.width - 12, y + row.height + 4}, 0xd6c7a6);
        }
    }
    target_->PopAxisAlignedClip();
    if (maximum > 0) {
        const float thumb = std::max(20.0f, area.height * area.height / page.height);
        fill({area.x + area.width - 5, area.y, 4, area.height}, 0xd6c7a6);
        fill({area.x + area.width - 5, area.y + (area.height - thumb) * offset / maximum, 4, thumb}, 0x8a7455);
        text(L"Колесо — листать карточку", rect(782, 679, 285, 22), 0x8a7455);
        const auto arrow = [&](UiRect bounds, const wchar_t* caption, bool enabled) {
            fill(bounds, enabled && bounds.contains(mouse) ? 0xc4ad82 : 0xdfcdaa);
            centeredText(caption, bounds, enabled ? 0x392f25 : 0xaa9674);
        };
        arrow(layout.detailsUp, L"↑", offset > 0);
        arrow(layout.detailsDown, L"↓", offset < maximum);
    }
    text(std::to_wstring(entryIndex + 1) + L" / " + std::to_wstring(entries.size()), rect(1176, 679, 54, 22), 0x8a7455);
    std::wstring commander;
    for (const auto& c : definitions.commanders()) if (c.factionId == faction.id) {
        if (!commander.empty()) commander += L", ";
        commander += wide(c.displayName);
    }
    text(L"Командир: " + commander, rect(294, 742, 890, 26), 0xccb68d);
    drawColorSelect(ColorSelectLayout(layout.color, 800), menu, mouse);
    target_->SetTransform(previous);
}
}
