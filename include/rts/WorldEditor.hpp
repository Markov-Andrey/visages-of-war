#pragma once
#include "rts/Map.hpp"
#include "rts/WorldAssets.hpp"
#include "rts/Definitions.hpp"
#include "rts/Menu.hpp"

namespace rts {
enum class EditorTool { Paint, ErasePaint, Decoration, Environment, Unit, Height, Surface, Ramp, Remove, Start, Base };
struct EditorChoice { std::string id, name; };
struct EditorLayout {
    UiRect world, panel;
    std::array<UiRect, 8> actions;
    std::array<UiRect, 11> tools;
    std::array<UiRect, 8> choices;
    UiRect previous, next, radiusMinus, radiusPlus, opacityMinus, opacityPlus, valueMinus, valuePlus;
    explicit EditorLayout(Vec2 size);
};
class WorldEditor {
public:
    WorldEditor(Scenario scenario, const WorldAssets& assets, const Definitions& definitions);
    const Scenario& scenario() const { return scenario_; }
    const WorldAssets& assets() const { return *assets_; }
    const Definitions& definitions() const { return *definitions_; }
    void replace(Scenario scenario);
    void reloadDefinitions(const WorldAssets& assets);
    void beginStroke();
    bool apply(Vec2 position);
    void endStroke();
    bool undo();
    bool redo();
    void validateForPlay() const;
    std::vector<EditorChoice> choices() const;
    void setTool(EditorTool value) { endStroke(); tool=value; choice=0; }
    std::string selected() const;
    EditorTool tool = EditorTool::Paint;
    size_t choice{};
    float radius = 1.5f, opacity = .65f, hardness = .4f, scale = 1, rotation{};
    int height{}, direction{}, owner{};
    bool dirty{};
    std::wstring message = L"Кисть: ЛКМ и движение. Камера: WASD / средняя кнопка. Колесо: масштаб.";
    std::filesystem::path file;
private:
    bool applyOne(Vec2 point);
    EntityId nextId() const;
    Scenario scenario_;
    const WorldAssets* assets_;
    const Definitions* definitions_;
    std::vector<Scenario> undo_, redo_;
    std::optional<Scenario> before_;
    std::optional<Vec2> last_;
    bool strokeChanged_{};
};
}
