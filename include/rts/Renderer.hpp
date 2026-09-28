#pragma once
#include "rts/Paths.hpp"
#include "rts/Simulation.hpp"
#include "rts/Definitions.hpp"
#include "rts/Menu.hpp"
#include "rts/GameplayUi.hpp"
#include "rts/FogMask.hpp"
#include "rts/MinimapRaster.hpp"
#include "rts/WorldAssets.hpp"
#include "rts/TerrainPaint.hpp"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <span>
#include <tuple>

namespace rts {
class WorldEditor;
class Renderer {
public:
    explicit Renderer(Paths paths);
    void attach(HWND window);
    void resize(unsigned width, unsigned height);
    void draw(const Simulation& game, const WorldView& view, std::optional<Cell> hover,
              const GameplayUi& ui, bool grid, bool paused);
    void verifyAssets();
    void snapshot(const Simulation& game, const std::filesystem::path& output, const Definitions* menuDefinitions = nullptr, bool grid = false,
        const GameplayUi* interfaceState = nullptr, MenuPage menuPage = MenuPage::BattleSetup);
    void drawMenu(const Simulation& game, const MenuState& menu, const Definitions& definitions, Vec2 mouse);
    void drawEditor(const WorldEditor& editor, const WorldView& view, Vec2 mouse, bool grid);
    void reloadWorldAssets(const WorldAssets& assets);
    void validateWorldAssets(const WorldAssets& assets);
    void validateCombatAssets(const Definitions& definitions);
    void snapshotEditor(const WorldEditor& editor, const std::filesystem::path& output);
    Vec2 size() const;
private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    void ensureTarget();
    void loadResources();
    void updateFogMask(const Simulation& game);
    void discardTarget();
    void loadBitmap(const std::filesystem::path& path, ComPtr<ID2D1Bitmap>& bitmap, unsigned teamMask = 0, SpriteTeamMask palette = SpriteTeamMask::Blue);
    ID2D1Bitmap* unitBitmap(const UnitSpriteDefinition& definition, unsigned color);
    void unitImage(const UnitSpriteDefinition& definition, int column, int row, Vec2 ground, float zoom, unsigned color);
    void unitPortrait(const UnitSpriteDefinition& definition, Vec2 topLeft, Vec2 extent, unsigned color);
    void drawProjectiles(const Simulation& game, const WorldView& view);
    void polygon(std::span<const Vec2> points, unsigned color, float opacity = 1.0f, bool fill = true);
    void line(Vec2 a, Vec2 b, unsigned color, float width = 1.0f);
    void text(const std::wstring& value, D2D1_RECT_F rect, unsigned color, bool heading = false);
    void sprite(ID2D1Bitmap* bitmap, D2D1_RECT_F source, Vec2 topLeft, Vec2 extent, bool pixel = false);
    void buttonFrame(UiRect area);
    void tile(const Map& map, Cell c, const WorldView& view, bool grid, bool fog = false);
    void environmentObject(const EnvironmentObject& object, const Map& map, const WorldView& view);
    void hud(const Simulation& game, const GameplayUi& ui, bool paused, const WorldView& view, bool grid);
    void drawMinimap(const Simulation& game, const BattleLayout& layout, const WorldView& view);
    void buildingSprite(const Simulation& game, const Building& building, const WorldView& view, bool selected);
    void unitSprite(const Simulation& game, const Unit& unit, const WorldView& view, bool selected);
    void prepareLandscape(const Landscape& landscape, const Map& map);
    void paintedTile(Cell cell);
    void decoration(const Decoration& object, const Map& map, const WorldView& view);
    void worldSprite(const WorldObjectDefinition& definition, Vec2 position, float scale, float rotation, const Map& map, const WorldView& view);
    void writeSnapshot(IWICBitmap* bitmap, const std::filesystem::path& output);
    struct MaterialResource { MaterialPixels image; ComPtr<ID2D1Bitmap> bitmap; ComPtr<ID2D1BitmapBrush> brush; };
    MaterialResource& materialResource(const std::string& id);
    struct PaintResource { uint64_t revision{}; ComPtr<ID2D1Bitmap> bitmap; ComPtr<ID2D1BitmapBrush> brush; };
    Paths paths_;
    WorldAssets worldAssets_;
    std::map<std::string,MaterialResource> materialResources_;
    std::map<std::filesystem::path,ComPtr<ID2D1Bitmap>> worldSprites_;
    std::map<std::tuple<std::filesystem::path,unsigned,SpriteTeamMask>,ComPtr<ID2D1Bitmap>> unitSheets_;
    TerrainPaint terrainPaint_;
    std::map<TerrainPaint::Key,PaintResource> paintResources_;
    HWND window_{};
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> writeFactory_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IDWriteTextFormat> bodyFormat_, titleFormat_;
    ComPtr<ID2D1RenderTarget> target_;
    ComPtr<ID2D1HwndRenderTarget> windowTarget_;
    Vec2 offscreenSize_{};
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1BitmapBrush> groundBrush_;
    FogMask fogMask_;
    MinimapRaster minimapRaster_;
    ComPtr<ID2D1Bitmap> fogBitmap_;
    ComPtr<ID2D1BitmapBrush> fogBrush_;
    ComPtr<ID2D1Bitmap> hall_, crystal_, worker_, enemy_, tree_, minimap_;
    ComPtr<ID2D1Bitmap> menuBackground_, logo_, buttonFrame_;
    unsigned teamColor_ = teamRgb(TeamColor::Blue);
    unsigned enemyColor_ = teamRgb(TeamColor::Red);
    float worldOpacity_ = 1;
};
}
