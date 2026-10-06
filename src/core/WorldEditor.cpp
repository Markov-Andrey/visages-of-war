#include "rts/WorldEditor.hpp"
#include "rts/Simulation.hpp"
#include <algorithm>
#include <stdexcept>

namespace rts {
WorldView editorOverview(const Map& map, UiRect area) {
    const float zoom=std::min((area.width-40)/(WorldView::tileSize*map.width()),(area.height-40)/(WorldView::tileSize*map.height()));
    return {{area.x+(area.width-WorldView::tileSize*map.width()*zoom)*.5f,
             area.y+(area.height-WorldView::tileSize*map.height()*zoom)*.5f},zoom};
}
EditorLayout::EditorLayout(Vec2 size) {
    const float width=286;
    world={0,60,std::max(1.0f,size.x-width),std::max(1.0f,size.y-94)};
    panel={world.width,60,width,size.y-60};
    const float actionStep=std::min(111.0f,(size.x-16)/actions.size());
    for(size_t i=0;i<actions.size();++i) actions[i]={8+i*actionStep,10,actionStep-7,38};
    for(size_t i=0;i<tools.size();++i) tools[i]={panel.x+10+(i%2)*134.0f,74+(i/2)*35.0f,128,30};
    previousGroup={panel.x+10,280,28,24}; nextGroup={panel.x+248,280,28,24};
    for(size_t i=0;i<choices.size();++i) choices[i]={panel.x+10,340+i*30.0f,266,27};
    previous={panel.x+10,310,128,24}; next={panel.x+148,310,128,24};
    radiusMinus={panel.x+12,560,32,28}; radiusPlus={panel.x+242,560,32,28};
    opacityMinus={panel.x+12,598,32,28}; opacityPlus={panel.x+242,598,32,28};
    valueMinus={panel.x+12,636,32,28}; valuePlus={panel.x+242,636,32,28};
}
WorldEditor::WorldEditor(Scenario scenario,const WorldAssets& assets,const Definitions& definitions)
    : scenario_(std::move(scenario)),assets_(&assets),definitions_(&definitions) { rebuild(scenario_); }
void WorldEditor::rebuild(Scenario& scenario) const {
    const auto& depot = startingDepot();
    rebuildScenario(scenario, {depot.width, depot.height});
}
void WorldEditor::replace(Scenario scenario) {
    rebuild(scenario); scenario_=std::move(scenario); undo_.clear(); redo_.clear(); before_.reset(); last_.reset(); dirty=false;
}
void WorldEditor::reloadDefinitions(const WorldAssets& assets, const Definitions& definitions) {
    auto candidate=scenario_;
    assets.material(candidate.landscape.baseMaterial);
    for(const auto& p:candidate.landscape.paint) assets.material(p.material);
    for(const auto& d:candidate.landscape.decorations) if(assets.object(d.definitionId).gameplay) throw std::runtime_error("Decoration category changed");
    for(auto& o:candidate.environment) {
        const int hp=o.hitPoints; o=assets.instantiate(o.definitionId,o.id,o.origin); o.hitPoints=std::min(hp,o.maximumHitPoints);
    }
    for(auto& c:candidate.crystals) {
        const int remaining=c.remaining; c=assets.instantiateCrystal(c.definitionId,c.cell); c.remaining=std::min(remaining,c.capacity);
    }
    const auto& depot=definitions.startingDepot(definitions.commanders().front().factionId);
    rebuildScenario(candidate,{depot.width,depot.height});
    scenario_=std::move(candidate); assets_=&assets; definitions_=&definitions;
    undo_.clear(); redo_.clear(); before_.reset(); last_.reset(); choice=0; paletteGroup.clear();
}
std::vector<EditorChoice> WorldEditor::allChoices() const {
    std::vector<EditorChoice> result;
    if(tool==EditorTool::Paint || tool==EditorTool::ErasePaint || tool==EditorTool::Base)
        for(const auto& m:assets_->materials()) result.push_back({m.id,m.name,m.group});
    if(tool==EditorTool::Decoration || tool==EditorTool::Environment) {
        for(const auto& d:assets_->objects()) if(d.gameplay==(tool==EditorTool::Environment)) result.push_back({d.id,d.name,d.group});
        if(tool==EditorTool::Environment) for(const auto& d:assets_->crystalSprites())
            result.push_back({"$"+d.id,d.name+" / "+std::to_string(d.capacity),"Resources"});
    }
    if(tool==EditorTool::Unit) for(const auto& d:definitions_->entities()) if(d.mobile && d.width==1 && d.height==1) result.push_back({d.id,d.displayName});
    if(tool==EditorTool::Surface) result={{"land","Суша"},{"shallow","Мелководье"},{"deep","Глубокая вода"}};
    if(tool==EditorTool::Start) result={{"hall","Стартовая ратуша"},{"worker","Стартовый рабочий"},{"hero","Герой командира"}};
    return result;
}
std::vector<EditorChoice> WorldEditor::choices() const {
    auto result=allChoices();
    if(!paletteGroup.empty()) std::erase_if(result,[&](const auto& item) { return item.group!=paletteGroup; });
    return result;
}
std::vector<std::string> WorldEditor::groups() const {
    std::vector<std::string> result{std::string{}};
    for(const auto& item:allChoices())
        if(std::find(result.begin(),result.end(),item.group)==result.end()) result.push_back(item.group);
    return result;
}
void WorldEditor::cycleGroup(int step) {
    endStroke();
    const auto list=groups();
    const auto at=std::find(list.begin(),list.end(),paletteGroup);
    const int index=at==list.end()?0:int(at-list.begin());
    paletteGroup=list[(index+(step<0?-1:1)+int(list.size()))%list.size()]; choice=0;
}
std::string WorldEditor::selected() const { const auto list=choices(); return list.empty()?std::string{}:list[std::min(choice,list.size()-1)].id; }
void WorldEditor::beginStroke() {
    if(before_) return;
    before_=scenario_; strokeChanged_=false; last_.reset();
}
void WorldEditor::endStroke() {
    if(before_ && strokeChanged_) {
        if(undo_.size()>=32) undo_.erase(undo_.begin());
        undo_.push_back(std::move(*before_)); redo_.clear(); dirty=true;
    }
    before_.reset(); last_.reset(); strokeChanged_=false;
}
bool WorldEditor::undo() {
    endStroke(); if(undo_.empty()) return false;
    redo_.push_back(std::move(scenario_)); scenario_=std::move(undo_.back()); undo_.pop_back(); dirty=true; return true;
}
bool WorldEditor::redo() {
    endStroke(); if(redo_.empty()) return false;
    undo_.push_back(std::move(scenario_)); scenario_=std::move(redo_.back()); redo_.pop_back(); dirty=true; return true;
}
EntityId WorldEditor::nextId() const {
    EntityId id=1000;
    for(const auto& d:scenario_.environment) id=std::max(id,d.id+1);
    for(const auto& d:scenario_.landscape.decorations) id=std::max(id,d.id+1);
    return id;
}
bool WorldEditor::apply(Vec2 p) {
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.y<0||p.x>=scenario_.map.width()||p.y>=scenario_.map.height()) return false;
    beginStroke();
    const bool brush=tool==EditorTool::Paint||tool==EditorTool::ErasePaint||tool==EditorTool::Height||tool==EditorTool::Surface;
    bool changed=false;
    if(last_ && brush) {
        const Vec2 delta=p-*last_; const float distance=std::hypot(delta.x,delta.y);
        const float step=std::max(.08f,radius*.18f);
        if(distance<step) return false;
        const int count=static_cast<int>(std::ceil(distance/step));
        for(int i=1;i<=count;++i) changed=applyOne(*last_+delta*(float(i)/count))||changed;
    } else if(!last_) changed=applyOne(p);
    last_=p; strokeChanged_=strokeChanged_||changed; return changed;
}
bool WorldEditor::applyOne(Vec2 p) {
    const Cell c{int(std::floor(p.x)),int(std::floor(p.y))};
    const auto id=selected(); bool changed=false;
    try {
        // Cosmetic edits validate only the new data. beginStroke already owns
        // the single full snapshot needed for undo; existing derived maps stay valid.
        if(tool==EditorTool::Paint || tool==EditorTool::ErasePaint) {
            assets_->material(id);
            const PaintStamp stamp{id,p,radius,opacity,hardness,tool==EditorTool::ErasePaint};
            validatePaintStamp(scenario_.map,stamp);
            if(scenario_.landscape.paint.size()>=Landscape::maximumPaintStamps) throw std::runtime_error("Too many paint stamps");
            scenario_.landscape.paint.push_back(stamp); changed=true;
        } else if(tool==EditorTool::Base) {
            assets_->material(id); scenario_.landscape.baseMaterial=id; changed=true;
        }
        else if(tool==EditorTool::Decoration) {
            if(assets_->object(id).gameplay) throw std::runtime_error("Expected cosmetic asset");
            const Decoration decoration{nextId(),id,p,scale,rotation};
            validateDecoration(scenario_.map,decoration);
            scenario_.landscape.decorations.push_back(decoration); changed=true;
        }
        if(changed) { message=L"Изменение применено. Ctrl+Z — отмена, Ctrl+S — сохранить."; return true; }
        // Gameplay edits remain transactional: publish only after full validation.
        auto s=scenario_;
        if(tool==EditorTool::Environment) {
            if(id.starts_with("$")) s.crystals.push_back(assets_->instantiateCrystal(id.substr(1),c));
            else s.environment.push_back(assets_->instantiate(id,nextId(),c));
            changed=true;
        } else if(tool==EditorTool::Unit) {
            const auto& d=definitions_->entity(id);
            if(!s.map.walkable(c,d.movement)) throw std::runtime_error("Unit cannot stand on this surface");
            s.units.push_back({id,static_cast<PlayerId>(owner),c}); changed=true;
        } else if(tool==EditorTool::Start) {
            if(id=="hall") s.hall=c; else if(id=="worker") s.worker=c; else s.heroSpawn=c; changed=true;
        } else if(tool==EditorTool::Height || tool==EditorTool::Surface) {
            for(int y=std::max(0,int(p.y-radius));y<std::min(s.map.height(),int(std::ceil(p.y+radius)));++y)
                for(int x=std::max(0,int(p.x-radius));x<std::min(s.map.width(),int(std::ceil(p.x+radius)));++x) {
                    if(std::hypot(x+.5f-p.x,y+.5f-p.y)>radius || s.map.occupancy({x,y})) continue;
                    auto& t=s.map.at({x,y});
                    if(tool==EditorTool::Height) { changed=changed||t.height!=height; t.height=height; }
                    else { const auto surface=id=="land"?Surface::Land:id=="shallow"?Surface::ShallowWater:Surface::DeepWater; changed=changed||t.surface!=surface; t.surface=surface; }
                }
            for(int y=0;y<s.map.height();++y) for(int x=0;x<s.map.width();++x) {
                auto& t=s.map.at({x,y}); const Cell up=Cell{x,y}+t.ramp;
                if(t.ramp!=Cell{} && (!s.map.contains(up)||s.map.at(up).height!=t.height+1||t.surface!=Surface::Land||s.map.at(up).surface!=Surface::Land)) t.ramp={};
            }
        } else if(tool==EditorTool::Ramp) {
            constexpr std::array<Cell,4> directions{{{1,0},{0,1},{-1,0},{0,-1}}};
            const auto d=directions[direction%4]; const Cell side{-d.y,d.x};
            for(int i=-1;i<=1;++i) {
                const Cell at=c+Cell{side.x*i,side.y*i};
                if(!s.map.contains(at)||!s.map.contains(at+d)||s.map.occupancy(at)||s.map.occupancy(at+d)) throw std::runtime_error("Ramp needs three clear lanes");
                s.map.at(at).ramp=d;
            }
            changed=true;
        } else if(tool==EditorTool::Remove) {
            auto& decorations=s.landscape.decorations;
            auto nearest=decorations.end(); float distance=.8f;
            for(auto i=decorations.begin();i!=decorations.end();++i) { const float d=std::hypot(i->position.x-p.x,i->position.y-p.y); if(d<distance){nearest=i;distance=d;} }
            if(nearest!=decorations.end()) { decorations.erase(nearest); changed=true; }
            else {
                const auto count=s.environment.size()+s.crystals.size()+s.units.size();
                std::erase_if(s.environment,[&](const auto& o){return c.x>=o.origin.x&&c.y>=o.origin.y&&c.x<o.origin.x+o.width&&c.y<o.origin.y+o.height;});
                std::erase_if(s.crystals,[&](const auto& v){return v.contains(c);}); std::erase_if(s.units,[&](const auto& v){return v.cell==c;});
                changed=count!=s.environment.size()+s.crystals.size()+s.units.size();
                if(s.map.at(c).ramp!=Cell{}) { s.map.at(c).ramp={}; changed=true; }
            }
        }
        if(!changed) return false;
        rebuild(s);
        // Simulation is the authority for spawn movement layers, duplicate slots and supply.
        {
            const auto& commander=definitions_->commanders().front();
            Simulation check(s,{},definitions_->entity(commander.startingWorker),definitions_->entities(),s.heroSpawn?commander.startingHero:std::string{},definitions_->progression());
        }
        scenario_=std::move(s); message=L"Изменение применено. Ctrl+Z — отмена, Ctrl+S — сохранить."; return true;
    } catch(const std::exception&) { message=L"Нельзя разместить здесь: проверьте поверхность, занятые места и лимит армии. Для рампы нужны 3 полосы у перепада +1."; return false; }
}
void WorldEditor::validateForPlay() const {
    auto s=scenario_; rebuild(s);
    const auto& commander=definitions_->commanders().front();
    Simulation check(s,{},definitions_->entity(commander.startingWorker),definitions_->entities(),s.heroSpawn?commander.startingHero:std::string{},definitions_->progression());
}
}
