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
    prepareLandscape(s.landscape,map);
    const auto extent=size(); const EditorLayout layout(extent);
    target_->BeginDraw(); target_->Clear(D2D1::ColorF(0x081119)); worldOpacity_=1;
    target_->PushAxisAlignedClip(rect(layout.world.x,layout.world.y,layout.world.width,layout.world.height),D2D1_ANTIALIAS_MODE_ALIASED);
    struct Item { float depth; int kind; size_t index; };
    std::vector<Item> items;
    for(size_t i=0;i<s.environment.size();++i) items.push_back({s.environment[i].origin.y+s.environment[i].height-.5f,0,i});
    for(size_t i=0;i<s.landscape.decorations.size();++i) items.push_back({s.landscape.decorations[i].position.y,1,i});
    for(size_t i=0;i<s.crystals.size();++i) items.push_back({s.crystals[i].cell.y+.5f,2,i});
    items.push_back({s.hall.y+1.5f,3,0});
    std::vector<UnitSpawn> units=s.units;
    const auto& commander=editor.definitions().commanders().front();
    units.push_back({commander.startingWorker,0,s.worker});
    for(auto c:s.extraWorkers) units.push_back({commander.startingWorker,0,c});
    if(s.heroSpawn) units.push_back({commander.startingHero,0,*s.heroSpawn});
    for(size_t i=0;i<units.size();++i) if(!airborne(editor.definitions().entity(units[i].definitionId).movement)) items.push_back({units[i].cell.y+.5f,4,i});
    std::stable_sort(items.begin(),items.end(),[](auto a,auto b){return a.depth<b.depth;});
    const auto visible=[&](Vec2 p){return p.x>-200&&p.x<layout.world.width+200&&p.y>-100&&p.y<extent.y+180;};
    const auto drawUnit=[&](const UnitSpawn& u) {
        const auto& d=editor.definitions().entity(u.definitionId); const bool air=airborne(d.movement);
        const auto ground=view.project(center(u.cell),map.surfaceHeight(u.cell,center(u.cell)));
        const auto p=air?view.project(center(u.cell),5):ground;
        if(!visible(p)) return;
        if(air) { line(p,ground,0x76d99b,1.5f); brush_->SetColor(D2D1::ColorF(0x76d99b)); target_->DrawEllipse(D2D1::Ellipse(point(p),20*view.zoom,8*view.zoom),brush_.Get()); }
        unitImage(d.sprite,d.sprite.idle,d.sprite.rows[0],p,view.zoom,u.owner==0?teamColor_:enemyColor_);
        if(d.hero) { brush_->SetColor(D2D1::ColorF(0xe8c56b)); target_->DrawEllipse(D2D1::Ellipse(point(p),22*view.zoom,9*view.zoom),brush_.Get(),2); }
    };
    size_t next=0;
    for(int y=0;y<map.height();++y) {
        for(int x=0;x<map.width();++x) if(visible(view.project(center({x,y}),float(map.at({x,y}).height)))) tile(map,{x,y},view,grid);
        while(next<items.size()&&items[next].depth<y+1) {
            const auto item=items[next++];
            if(item.kind==0) environmentObject(s.environment[item.index],map,view);
            if(item.kind==1) decoration(s.landscape.decorations[item.index],map,view);
            if(item.kind==2) { const auto c=s.crystals[item.index].cell; const auto p=view.project(center(c),float(map.at(c).height)); sprite(crystal_.Get(),rect(0,0,128,128),p+Vec2{-48,-76}*view.zoom,Vec2{96,96}*view.zoom); }
            if(item.kind==3) { const auto p=view.project({s.hall.x+1.0f,s.hall.y+1.0f},float(map.at(s.hall).height)); sprite(hall_.Get(),rect(96,104,312,344),p+Vec2{-96,-145}*view.zoom,Vec2{192,212}*view.zoom); }
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
        else if(editor.tool!=EditorTool::Base) polygon(map.surfaceCorners(c,view),0xace8d0,.7f,false);
        if(editor.tool==EditorTool::Unit&&!editor.selected().empty()) {
            worldOpacity_=.55f; drawUnit({editor.selected(),static_cast<PlayerId>(editor.owner),c}); worldOpacity_=1;
        }
        if(editor.tool==EditorTool::Decoration && !editor.selected().empty()) {
            worldOpacity_=.55f; worldSprite(worldAssets_.object(editor.selected()),*at,editor.scale,editor.rotation,map,view); worldOpacity_=1;
        }
        if(editor.tool==EditorTool::Environment && editor.selected()!="$crystal" && !editor.selected().empty()) {
            const auto& d=worldAssets_.object(editor.selected());
            for(int y=0;y<d.height;++y) for(int x=0;x<d.width;++x) if(map.contains(c+Cell{x,y})) polygon(map.surfaceCorners(c+Cell{x,y},view),0x9de0c1,.4f,false);
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
    const auto choices=editor.choices(); const size_t first=editor.choice/8*8;
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
    text(L"R — поворот декора / рампы\n[ ] — размер кисти\nG — сетка, F5 — перечитать ассеты\nCtrl+Z / Y — отмена / повтор\nF9 — тест, F10 — назад в редактор",
        rect(layout.panel.x+12,682,264,112),0x8ca9ae);
    panel({0,extent.y-34,layout.world.width,34},0x15272f);
    text(editor.message,rect(12,extent.y-28,layout.world.width-24,26),0xc1d9d0);
    text(L"VISAGES FORGE  /  "+std::wstring(editor.dirty?L"* ":L"")+wide(s.name),rect(910,18,std::max(0.0f,extent.x-920),30),0xe7cf9d);
    const auto hr=target_->EndDraw(); if(hr==D2DERR_RECREATE_TARGET) discardTarget(); else check(hr);
}
void Renderer::snapshotEditor(const WorldEditor& editor,const std::filesystem::path& output) {
    discardTarget(); offscreenSize_={1440,900};
    ComPtr<IWICBitmap> bitmap; check(wic_->CreateBitmap(1440,900,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf()));
    auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE); properties.dpiX=properties.dpiY=96;
    check(factory_->CreateWicBitmapRenderTarget(bitmap.Get(),properties,target_.GetAddressOf())); loadResources();
    WorldView view{{0,0},.75f}; view.origin=Vec2{560,300}-view.project(center(editor.scenario().hall)+Vec2{1,2},0);
    drawEditor(editor,view,{600,510},false); writeSnapshot(bitmap.Get(),output); discardTarget(); offscreenSize_={};
}
}
