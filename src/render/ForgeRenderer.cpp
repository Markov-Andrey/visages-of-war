#include "RenderSupport.hpp"
#include "rts/WorldEditor.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace rts {
using namespace render;
namespace {
std::wstring decimal(float value) { std::wostringstream s; s<<std::fixed<<std::setprecision(1)<<value; return s.str(); }
}

void Renderer::drawEditor(const WorldEditor& editor,const WorldView& view,Vec2 mouse,bool grid) {
    ensureTarget();
    const auto& s=editor.scenario(); const auto& map=s.map;
    const auto& depot=editor.startingDepot();
    prepareLandscape(s.landscape,map);
    waterSeconds_ = 0;
    const auto extent=size(); const EditorLayout layout(extent);
    target_->BeginDraw(); target_->Clear(D2D1::ColorF(0x081119)); worldOpacity_=1;
    target_->PushAxisAlignedClip(rect(layout.world.x,layout.world.y,layout.world.width,layout.world.height),D2D1_ANTIALIAS_MODE_ALIASED);
    struct Item { float depth; int kind; size_t index; };
    std::vector<Item> items;
    for(size_t i=0;i<s.environment.size();++i) items.push_back({s.environment[i].origin.y+s.environment[i].height-.5f,0,i});
    for(size_t i=0;i<s.landscape.decorations.size();++i) items.push_back({s.landscape.decorations[i].position.y,1,i});
    for(size_t i=0;i<s.crystals.size();++i) items.push_back({s.crystals[i].depth(),2,i});
    items.push_back({s.hall.y+depot.height-.5f,3,0});
    std::vector<UnitSpawn> units=s.units;
    const auto& commander=editor.definitions().commanders().front();
    units.push_back({commander.startingWorker,0,s.worker});
    for(auto c:s.extraWorkers) units.push_back({commander.startingWorker,0,c});
    if(s.heroSpawn) units.push_back({commander.startingHero,0,*s.heroSpawn});
    for(size_t i=0;i<units.size();++i) if(!airborne(editor.definitions().entity(units[i].definitionId).movement)) items.push_back({unitDrawDepth(center(units[i].cell)),4,i});
    std::stable_sort(items.begin(),items.end(),[](auto a,auto b){return a.depth<b.depth;});
    const auto visible=[&](Vec2 p){return p.x>-200&&p.x<layout.world.width+200&&p.y>-100&&p.y<extent.y+180;};
    const auto drawUnit=[&](const UnitSpawn& u) {
        const auto& d=editor.definitions().entity(u.definitionId); const bool air=airborne(d.movement);
        const auto ground=unitScreenAnchor(view,center(u.cell),map.surfaceHeight(u.cell,center(u.cell)));
        const auto p=unitScreenAnchor(view,center(u.cell),map.movementHeight(center(u.cell),d.movement));
        if(!visible(p)) return;
        if(air) { line(p,ground,0x76d99b,1.5f); brush_->SetColor(D2D1::ColorF(0x76d99b)); target_->DrawEllipse(D2D1::Ellipse(point(p),20*view.zoom,8*view.zoom),brush_.Get()); }
        const float immersion = air ? 0.0f : std::max(0.0f,
            map.surfaceHeight(u.cell,center(u.cell)) - map.movementHeight(center(u.cell),d.movement));
        wadingUnitImage(d.sprite,d.sprite.idle,d.sprite.rows[0],p,ground,immersion,view.zoom,u.owner==0?teamColor_:enemyColor_);
        if(d.hero) { brush_->SetColor(D2D1::ColorF(0xe8c56b)); target_->DrawEllipse(D2D1::Ellipse(point(p),22*view.zoom,9*view.zoom),brush_.Get(),2); }
    };
    size_t next=0;
    for(int y=0;y<map.height();++y) {
        terrainRow(map,y,view,grid);
        while(next<items.size()&&items[next].depth<y+1) {
            const auto item=items[next++];
            if(item.kind==0) environmentObject(s.environment[item.index],map,view);
            if(item.kind==1) decoration(s.landscape.decorations[item.index],map,view);
            if(item.kind==2) { const auto& crystal=s.crystals[item.index]; const auto p=view.project(crystal.center(),float(map.at(crystal.cell).height)); drawCrystal(crystal,p,view.zoom); }
            if(item.kind==3) {
                const auto p=view.project({s.hall.x+depot.width*.5f,s.hall.y+depot.height*.5f},float(map.at(s.hall).height));
                const auto* stage=depot.buildingSprite.stage(depot.constructionTicks,depot.constructionTicks);
                if(stage) buildingImage(*stage,buildingStageBounds(*stage,depot.buildingSprite.scale,p,view.zoom),teamColor_);
                else sprite(hall_.Get(),rect(96,104,312,344),p+Vec2{-96,-145}*view.zoom,Vec2{192,212}*view.zoom);
            }
            if(item.kind==4) drawUnit(units[item.index]);
        }
    }
    for(const auto& u:units) if(airborne(editor.definitions().entity(u.definitionId).movement)) drawUnit(u);
    if(grid) for(const auto& o:s.environment) if(o.active()) for(int y=0;y<o.height;++y) for(int x=0;x<o.width;++x)
        polygon(map.surfaceCorners(o.origin+Cell{x,y},view),o.blocks(x,y)?0xe78678:0x7ad6a3,.55f,false);
    if(layout.world.contains(mouse)) if(const auto at=map.pickPosition(mouse,view)) {
        const Cell c{int(at->x),int(at->y)}; const auto p=view.project(*at,map.surfaceHeight(c,*at));
        brush_->SetColor(D2D1::ColorF(0xace8d0,.9f));
        if(editor.tool==EditorTool::Paint||editor.tool==EditorTool::ErasePaint||editor.tool==EditorTool::Height||editor.tool==EditorTool::Surface)
            target_->DrawEllipse(D2D1::Ellipse(point(p),editor.radius*64*view.zoom,editor.radius*64*view.zoom),brush_.Get(),1.5f);
        else if(editor.tool==EditorTool::Start && editor.selected()=="hall") {
            for(int y=0;y<depot.height;++y) for(int x=0;x<depot.width;++x)
                if(map.contains(c+Cell{x,y})) polygon(map.surfaceCorners(c+Cell{x,y},view),0xace8d0,.7f,false);
        } else if(editor.tool!=EditorTool::Base) polygon(map.surfaceCorners(c,view),0xace8d0,.7f,false);
        if(editor.tool==EditorTool::Unit&&!editor.selected().empty()) {
            worldOpacity_=.55f; drawUnit({editor.selected(),static_cast<PlayerId>(editor.owner),c}); worldOpacity_=1;
        }
        if(editor.tool==EditorTool::Decoration && !editor.selected().empty()) {
            worldOpacity_=.55f; worldSprite(worldAssets_.object(editor.selected()),*at,editor.scale,editor.rotation,map,view); worldOpacity_=1;
        }
        if(editor.tool==EditorTool::Environment && !editor.selected().empty()) {
            const bool resource=editor.selected().starts_with("$");
            const auto footprint=[&] {
                if(resource) { const auto& d=worldAssets_.crystalSprite(editor.selected().substr(1)); return Cell{d.width,d.height}; }
                const auto& d=worldAssets_.object(editor.selected()); return Cell{d.width,d.height};
            }();
            for(int y=0;y<footprint.y;++y) for(int x=0;x<footprint.x;++x) if(map.contains(c+Cell{x,y})) polygon(map.surfaceCorners(c+Cell{x,y},view),0x9de0c1,.4f,false);
            if(resource) {
                const auto node=worldAssets_.instantiateCrystal(editor.selected().substr(1),c);
                worldOpacity_=.55f; drawCrystal(node,view.project(node.center(),float(map.at(c).height)),view.zoom); worldOpacity_=1;
            }
        }
    }
    target_->PopAxisAlignedClip();
    const auto panel=[&](UiRect a,unsigned color) { brush_->SetColor(D2D1::ColorF(color)); target_->FillRectangle(rect(a.x,a.y,a.width,a.height),brush_.Get()); };
    const auto button=[&](UiRect a,const std::wstring& title,bool active=false) {
        panel(a,a.contains(mouse)?0x35594f:active?0x38534f:0x20343e);
        text(title,rect(a.x+8,a.y+6,a.width-12,a.height-6),active?0xffe0a3:0xdde7df);
    };
    panel({0,0,extent.x,60},0x101c25); panel(layout.panel,0x101c25);
    constexpr std::array<const wchar_t*,8> actions{L"Выйти",L"Новая",L"Открыть",L"Сохранить",L"Как…",L"Тест / F9",L"Отмена",L"Повтор"};
    for(size_t i=0;i<actions.size();++i) button(layout.actions[i],actions[i]);
    constexpr std::array<const wchar_t*,11> tools{L"Текстура",L"Ластик",L"Декор",L"Игровой объект",L"Юниты",L"Высота",L"Поверхность",L"Рампа ×3",L"Удалить",L"Точка старта",L"Подложка"};
    for(size_t i=0;i<tools.size();++i) button(layout.tools[i],tools[i],static_cast<size_t>(editor.tool)==i);
    button(layout.previous,L"◀ Предыдущие"); button(layout.next,L"Следующие ▶");
    button(layout.previousGroup,L"<"); button(layout.nextGroup,L">");
    text(wide(editor.paletteGroup.empty()?"All assets":editor.paletteGroup),rect(layout.panel.x+43,285,202,24),0xe7cf9d);
    const auto choices=editor.choices(); const size_t first=editor.choice/layout.choices.size()*layout.choices.size();
    for(size_t i=0;i<layout.choices.size();++i) if(first+i<choices.size()) button(layout.choices[i],wide(choices[first+i].name),first+i==editor.choice);
    button(layout.radiusMinus,L"−"); button(layout.radiusPlus,L"+");
    button(layout.opacityMinus,L"−"); button(layout.opacityPlus,L"+");
    button(layout.valueMinus,L"−"); button(layout.valuePlus,L"+");
    text(L"Кисть: "+decimal(editor.radius)+L" кл.",rect(layout.panel.x+52,565,180,25),0xaac8be);
    text(L"Плотность: "+std::to_wstring(int(editor.opacity*100))+L"%",rect(layout.panel.x+52,603,180,25),0xaac8be);
    std::wstring value=L"Высота: "+std::to_wstring(editor.height);
    if(editor.tool==EditorTool::Paint||editor.tool==EditorTool::ErasePaint) value=L"Жёсткость: "+std::to_wstring(int(editor.hardness*100))+L"%";
    if(editor.tool==EditorTool::Decoration) value=L"Размер: "+decimal(editor.scale);
    if(editor.tool==EditorTool::Unit) value=editor.owner==0?L"Свой игрок":L"Противник";
    if(editor.tool==EditorTool::Ramp) { constexpr std::array<const wchar_t*,4> directions{L"вправо",L"вниз",L"влево",L"вверх"}; value=L"Подъём "+std::wstring(directions[editor.direction%4]); }
    text(value,rect(layout.panel.x+52,641,180,25),0xe7cf9d);
    text(L"End / Home - overview / start\nR - rotate; [ ] - brush size\nK - grid; F5 - reload assets\nCtrl+Z / Y - undo / redo\nF9 - play; F10 - return",
        rect(layout.panel.x+12,682,264,112),0x8ca9ae);
    panel({0,extent.y-34,layout.world.width,34},0x15272f);
    text(editor.message,rect(12,extent.y-28,layout.world.width-24,26),0xc1d9d0);
    text(L"VISAGES FORGE  /  "+std::wstring(editor.dirty?L"* ":L"")+wide(s.name),rect(910,18,std::max(0.0f,extent.x-920),30),0xe7cf9d);
    const auto hr=target_->EndDraw(); if(hr==D2DERR_RECREATE_TARGET) discardTarget(); else check(hr);
}
void Renderer::snapshotEditor(const WorldEditor& editor,const std::filesystem::path& output,bool overview) {
    discardTarget(); offscreenSize_={1440,900};
    ComPtr<IWICBitmap> bitmap; check(wic_->CreateBitmap(1440,900,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf()));
    auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE); properties.dpiX=properties.dpiY=96;
    check(factory_->CreateWicBitmapRenderTarget(bitmap.Get(),properties,target_.GetAddressOf())); loadResources();
    WorldView view{{0,0},.75f}; view.origin=Vec2{560,300}-view.project(center(editor.scenario().hall)+Vec2{1,2},0);
    if(overview) view=editorOverview(editor.scenario().map,EditorLayout({1440,900}).world);
    drawEditor(editor,view,{-1,-1},false); writeSnapshot(bitmap.Get(),output); discardTarget(); offscreenSize_={};
}
}
