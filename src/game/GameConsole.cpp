#include "GameApplication.hpp"
#include <algorithm>
#include <cwctype>
#include <sstream>

namespace rts::game {
bool GameApplication::consoleKey(WPARAM key) {
    if (menu_.page != MenuPage::Playing) return false;
    if (!ui_.consoleOpen) {
        if (key != VK_RETURN) return false;
        camera_.stop(view_);
        dragging_ = panning_ = minimapDragging_ = false;
        ui_.drag.reset(); ReleaseCapture();
        clearCommandMode();
        ui_.consoleInput.clear(); ui_.consoleReply.clear(); ui_.consoleOpen = true;
        return true;
    }
    if (key == VK_ESCAPE) {
        ui_.consoleOpen = false; ui_.consoleInput.clear(); ui_.consoleReply.clear();
    } else if (key == VK_RETURN) {
        auto command = ui_.consoleInput;
        const auto first = command.find_first_not_of(L" \t"), last = command.find_last_not_of(L" \t");
        command = first == std::wstring::npos ? std::wstring{} : command.substr(first, last - first + 1);
        std::transform(command.begin(), command.end(), command.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (command == L"iseedeadpeople") game_.revealMap();
        else if (command.starts_with(L"buildpreview")) {
            std::wistringstream input(command);
            std::wstring name, value, extra; input >> name >> value >> extra;
            int percent = -1;
            if (!value.empty() && value.size() <= 3 && value.find_first_not_of(L"0123456789") == std::wstring::npos)
                percent = std::stoi(value);
            if (name != L"buildpreview" || !extra.empty() || (value != L"off" && value != L"play" && (percent < 0 || percent > 100))) {
                ui_.consoleReply = L"buildpreview 0..100 | play | off — для выбранных зданий";
                return true;
            }
            if (value == L"off") ui_.constructionPreviews.clear();
            else {
                bool changed = false;
                for (auto id : ui_.selection.ids) if (const auto* b = game_.building(id); b && b->owner == game_.player().id && b->definition.buildingSprite.construction) {
                    ui_.constructionPreviews[id] = {percent / 100.f, value == L"play", game_.clock().elapsedTicks()};
                    changed = true;
                }
                if (!changed) { ui_.consoleReply = L"Выберите ратушу или Миротворческий корпус"; return true; }
            }
        }
        else if (!command.empty()) {
            ui_.consoleReply = L"Неизвестная команда";
            return true;
        }
        ui_.consoleOpen = false; ui_.consoleInput.clear(); ui_.consoleReply.clear();
    }
    return true; // Typed hotkeys belong exclusively to the console.
}
void GameApplication::consoleCharacter(wchar_t character) {
    if (character == L'\b') {
        if (!ui_.consoleInput.empty()) ui_.consoleInput.pop_back();
    } else if (character >= L' ' && character != 127 && ui_.consoleInput.size() < 128) {
        ui_.consoleInput.push_back(character);
    }
    // WM_CHAR '\r' follows the opening/submitting keydown and must not toggle again.
}
}
