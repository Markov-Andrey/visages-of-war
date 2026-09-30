#include "RenderSupport.hpp"

namespace rts {
using namespace render;
void Renderer::drawColorSelect(const ColorSelectLayout& layout, const MenuState& menu, Vec2 mouse) {
    const auto fill = [&](UiRect area, unsigned color) {
        brush_->SetColor(D2D1::ColorF(color));
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect(area.x, area.y, area.width, area.height), 4, 4), brush_.Get());
    };
    const auto outline = [&](UiRect area, unsigned color, float width) {
        brush_->SetColor(D2D1::ColorF(color));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(rect(area.x, area.y, area.width, area.height), 4, 4), brush_.Get(), width);
    };
    const auto selected = static_cast<size_t>(menu.player.color);
    const auto field = layout.field;
    fill(field, field.contains(mouse) ? 0x2b3d3e : 0x1c2b30);
    outline(field, menu.colorDropdown || field.contains(mouse) ? 0xd6b57a : 0x8c7957, 1);
    fill({field.x + 12, field.y + (field.height - 22) * .5f, 28, 22}, teamPalettes.at(selected).rgb);
    outline({field.x + 12, field.y + (field.height - 22) * .5f, 28, 22}, 0x89918d, 1);
    text(std::wstring(teamPalettes[selected].name), rect(field.x + 52, field.y + (field.height - 24) * .5f, field.width - 86, 24), 0xe2e6d5);
    const Vec2 arrow{field.x + field.width - 20, field.y + field.height * .5f};
    const float direction = menu.colorDropdown ? -1.0f : 1.0f;
    line(arrow + Vec2{-5, -2 * direction}, arrow + Vec2{0, 3 * direction}, 0xd6b57a, 1.5f);
    line(arrow + Vec2{0, 3 * direction}, arrow + Vec2{5, -2 * direction}, 0xd6b57a, 1.5f);
    if (!menu.colorDropdown) return;

    const auto popup = layout.popup;
    fill({popup.x + 4, popup.y + 5, popup.width, popup.height}, 0x080d12);
    fill(popup, 0x19272c);
    outline(popup, 0xa28a60, 1);
    const auto focus = layout.pick(mouse).value_or(menu.colorFocus);
    for (size_t i = 0; i < teamPalettes.size(); ++i) {
        const auto area = layout.option(i);
        fill(area, teamPalettes[i].rgb);
        outline(area, 0x667778, 1);
        if (i == selected) {
            // A check remains visible independently of the hover/keyboard outline.
            line({area.x + 8, area.y + 17}, {area.x + 13, area.y + 22}, 0x14232b, 4);
            line({area.x + 13, area.y + 22}, {area.x + 24, area.y + 10}, 0x14232b, 4);
            line({area.x + 8, area.y + 17}, {area.x + 13, area.y + 22}, 0xffffff, 2);
            line({area.x + 13, area.y + 22}, {area.x + 24, area.y + 10}, 0xffffff, 2);
        }
        if (i == focus || i == selected)
            outline({area.x - 3, area.y - 3, area.width + 6, area.height + 6}, i == focus ? 0xffffff : 0xd6b57a, 1.5f);
    }
    text(std::wstring(teamPalettes.at(focus).name), rect(popup.x + 12, popup.y + popup.height - 31, popup.width - 24, 24), 0xe2d3b4);
}
}
