#include "TestSupport.hpp"
#include "rts/GroundTextureProjection.hpp"

namespace rts::tests {
void editorTests(TestSuite& test, const TestContext& context) {
    const auto& assets=context.assets;
    const auto& worldPaths=context.worldPaths;
    const auto& worldAssets=context.worldAssets;
    const auto loadScenario = [&](const std::filesystem::path& file) { return rts::loadScenario(file, context.worldAssets, context.hallFootprint); };
    test("Palette groups expose every Sunny Hills category and preserve authored state", [&] {
        const auto definitions=rts::Definitions::load(assets/"data/catalog.json");
        rts::WorldEditor editor(loadScenario(assets/"maps/demo.rtsmap"),worldAssets,definitions);
        editor.setTool(rts::EditorTool::Environment);
        const auto all=editor.choices(); const auto groups=editor.groups();
        require(groups.size()>=7,"Sunny Hills categories missing from palette");
        size_t count=0;
        for(size_t i=1;i<groups.size();++i) {
            editor.cycleGroup(1); const auto choices=editor.choices();
            require(!choices.empty()&&editor.choice==0,"Group change retained an invalid selection");
            for(const auto& item:choices) require(item.group==editor.paletteGroup,"Unrelated asset leaked into group");
            count+=choices.size();
        }
        require(count==all.size()&&!editor.dirty&&!editor.undo(),"Filtering lost assets or mutated the document");
        editor.cycleGroup(1); require(editor.paletteGroup.empty()&&editor.choices().size()==all.size(),"All assets group did not wrap");
        editor.cycleGroup(-1); editor.setTool(rts::EditorTool::Decoration);
        require(editor.paletteGroup.empty(),"Changing tools retained a stale group filter");
    });
    test("Authored map round trips all landscape layers and preserves files on invalid save", [&] {
        rts::Scenario s{rts::Map(24,24),{1,1},{4,3},{}}; s.name="Поляна — тест"; s.heroSpawn=rts::Cell{5,3}; s.startingCrystals=300;
        for(int y=0;y<24;++y) for(int x=16;x<24;++x) s.map.at({x,y}).height=1;
        for(int y=10;y<=12;++y) s.map.at({15,y}).ramp={1,0};
        s.map.at({5,18})={-1,false,{},rts::Surface::ShallowWater}; s.map.at({6,18})={-1,false,{},rts::Surface::DeepWater};
        s.map.at({23,23}).blocked=true;
        s.environment={worldAssets.instantiate("tree",1500,{8,8}),worldAssets.instantiate("rock",1501,{12,12})}; s.environment[0].hitPoints=0;
        s.crystals={{{9,18},777}};
        s.units={{"human.peacemaker",1,{8,9}},{"human.flying_soldier",0,{8,9}}};
        s.landscape.baseMaterial="dark_grass";
        s.landscape.paint={{"earth",{8.137f,8.927f},1.8f,.7f,.3f,false},{"dark_grass",{8.82f,9.12f},.8f,.4f,.6f,false},{"grass",{8.33f,8.55f},.25f,.8f,.2f,true}};
        s.landscape.decorations={{1700,"small_bush",{8.137f,8.927f},.7f,35}};
        rts::rebuildScenario(s, context.hallFootprint);
        const auto file=worldPaths.writable(L"карты с пробелами/Лесная поляна.rtsmap");
        rts::saveScenario(s,file); const auto loaded=loadScenario(file);
        require(loaded.name==s.name && loaded.landscape.baseMaterial=="dark_grass" && loaded.landscape.paint==s.landscape.paint,"Terrain paint changed after saving");
        const auto& d=loaded.landscape.decorations.front();
        require(d.id==1700 && d.position==s.landscape.decorations[0].position && d.rotation==35 && d.scale==.7f,"Free decoration transform changed");
        require(loaded.environment[0].id==1500&&!loaded.environment[0].active()&&!loaded.map.blocksVision({8,8}),"Inactive tree identity changed");
        require(loaded.map.at({6,18}).surface==rts::Surface::DeepWater&&loaded.map.at({5,18}).height==-1&&loaded.map.at({15,11}).ramp==rts::Cell{1,0},"Water or ramps changed after saving");
        require(loaded.map.at({23,23}).blocked&&loaded.map.at({23,23}).height==1,"Blocked terrain lost its height");
        require(loaded.units.size()==2&&loaded.crystals[0].remaining==777&&loaded.heroSpawn==s.heroSpawn,"Spawn data changed after saving");
        std::ifstream before(file); const auto document=nlohmann::json::parse(before); before.close();
        rts::saveScenario(loaded,file); std::ifstream after(file); require(nlohmann::json::parse(after)==document,"Map serialization is not stable"); after.close();
        auto invalid=s; invalid.hall={23,23}; mustThrow([&]{rts::saveScenario(invalid,file);});
        require(loadScenario(file).name==s.name,"Rejected save damaged original map");
    });
    test("Editor reserves catalog depot dimensions across moves undo reload and serialization", [&] {
        CatalogFixture fixture(assets);
        fixture.entities["entities"][3]["construction"]["footprint"] = {4, 3};
        const auto definitions = fixture.load();
        rts::WorldEditor editor({rts::Map(20, 20), {1, 1}, {7, 6}, {}}, worldAssets, definitions);
        require(editor.scenario().map.occupancy({4, 3}) == 1 && editor.scenario().map.walkable({5, 3}), "Editor ignored custom footprint");
        editor.setTool(rts::EditorTool::Start); editor.choice = 0;
        require(!editor.apply({17.5f, 10.5f}), "Editor accepted footprint beyond map edge"); editor.endStroke();
        require(!editor.apply({4.5f, 4.5f}), "Editor placed a hall over a worker in its last column"); editor.endStroke();
        require(editor.apply({10.5f, 10.5f}), "Editor rejected valid larger depot"); editor.endStroke();
        require(editor.scenario().map.walkable({4, 3}) && editor.scenario().map.occupancy({13, 12}) == 1, "Moving depot retained old occupancy");
        require(editor.undo() && editor.scenario().map.occupancy({4, 3}) == 1 && editor.scenario().map.walkable({13, 12}), "Undo lost depot footprint");
        require(editor.redo(), "Redo failed");
        const auto file = worldPaths.writable(L"catalog-footprint.rtsmap");
        rts::saveScenario(editor.scenario(), file);
        const auto loaded = rts::loadScenario(file, worldAssets, {4, 3});
        require(loaded.hallFootprint == rts::Cell{4, 3} && loaded.map.occupancy({13, 12}) == 1, "Map reload lost catalog footprint");
        std::ifstream input(file); const auto document = Json::parse(input);
        require(document.at("start").size() == 4 && !document.contains("hallFootprint"), "Derived depot dimensions leaked into the authored map");
        fixture.entities["entities"][3]["construction"]["footprint"] = {3, 2};
        const auto smaller = fixture.load(); editor.reloadDefinitions(worldAssets, smaller);
        require(editor.scenario().map.walkable({13, 12}) && editor.scenario().map.occupancy({12, 11}) == 1, "Catalog reload retained larger collision");
        editor.validateForPlay();
    });
    test("Editor brush interpolates free strokes and undo restores the entire operation", [&] {
        const auto defs=rts::Definitions::load(assets/"data/catalog.json");
        rts::Scenario s{rts::Map(24,24),{1,1},{4,3},{}};
        rts::WorldEditor e(s,worldAssets,defs); e.choice=1; e.radius=.75f;
        e.beginStroke(); require(e.apply({8.13f,9.27f}),"Initial dab rejected"); require(e.apply({13.61f,10.35f}),"Continuous stroke rejected"); e.endStroke();
        const auto paint=e.scenario().landscape.paint;
        require(paint.size()>20&&paint.front().position==rts::Vec2{8.13f,9.27f},"Paint snapped to grid or skipped interpolation");
        for(size_t i=1;i<paint.size();++i) require(std::hypot(paint[i].position.x-paint[i-1].position.x,paint[i].position.y-paint[i-1].position.y)<=.14f,"Stroke has visible gaps");
        require(e.undo()&&e.scenario().landscape.paint.empty(),"Undo removed only one dab");
        require(e.redo()&&e.scenario().landscape.paint==paint,"Redo changed brush data");
        e.setTool(rts::EditorTool::ErasePaint); e.apply({9.31f,9.51f}); e.endStroke();
        require(e.scenario().landscape.paint.back().erase&&e.scenario().map.walkable({9,9})&&!e.scenario().map.blocksVision({9,9}),"Paint altered gameplay");
    });
    test("Cosmetic edits preserve authored blockers and undo all interpolated stamps", [&] {
        const auto defs=rts::Definitions::load(assets/"data/catalog.json");
        rts::Scenario s{rts::Map(24,24),{1,1},{4,3},{}};
        s.environment={worldAssets.instantiate("tree",1500,{8,8})};
        rts::WorldEditor e(s,worldAssets,defs);
        const auto original=e.scenario().map;
        const auto unchanged=[&] {
            for(int y=0;y<24;++y) for(int x=0;x<24;++x) {
                const rts::Cell c{x,y};
                require(e.scenario().map.occupancy(c)==original.occupancy(c) &&
                    e.scenario().map.blocksVision(c)==original.blocksVision(c),"Cosmetic edit changed derived blockers");
            }
        };
        for(auto tool:{rts::EditorTool::Paint,rts::EditorTool::ErasePaint,rts::EditorTool::Base,rts::EditorTool::Decoration}) {
            e.setTool(tool); require(e.apply({8.5f,8.5f}),"Cosmetic edit on blocker rejected");
            if(tool==rts::EditorTool::Paint || tool==rts::EditorTool::ErasePaint) require(e.apply({12.5f,8.5f}),"Stroke rejected");
            e.endStroke(); unchanged();
            const auto count=e.scenario().landscape.paint.size();
            require(e.undo(),"Cosmetic undo failed"); unchanged();
            require(e.redo() && e.scenario().landscape.paint.size()==count,"Cosmetic redo changed stroke"); unchanged();
        }
        require(e.scenario().landscape.decorations.front().id>1500,"Decoration ID collided with environment");
        e.validateForPlay();
    });
    test("Cosmetic fast path rejects invalid data without modifying document or history", [&] {
        const auto defs=rts::Definitions::load(assets/"data/catalog.json");
        rts::WorldEditor e({rts::Map(24,24),{1,1},{4,3},{}},worldAssets,defs);
        for(float invalid:{0.0f,17.0f,std::numeric_limits<float>::quiet_NaN()}) {
            e.radius=invalid; require(!e.apply({8.5f,8.5f}),"Invalid brush radius accepted"); e.endStroke();
        }
        e.radius=1; e.opacity=0; require(!e.apply({8.5f,8.5f}),"Invalid opacity accepted"); e.endStroke();
        e.opacity=1; e.hardness=2; require(!e.apply({8.5f,8.5f}),"Invalid hardness accepted"); e.endStroke();
        e.setTool(rts::EditorTool::Decoration); e.scale=0;
        require(!e.apply({8.5f,8.5f}),"Invalid decoration scale accepted"); e.endStroke();
        e.scale=1; e.rotation=361;
        require(!e.apply({8.5f,8.5f}),"Invalid decoration rotation accepted"); e.endStroke();
        require(e.scenario().landscape.paint.empty() && e.scenario().landscape.decorations.empty() && !e.undo() && !e.dirty,
            "Rejected cosmetic edit mutated document or history");
        auto full=e.scenario();
        full.landscape.paint.resize(rts::Landscape::maximumPaintStamps,{"earth",{8.5f,8.5f},1,.5f,.5f,false});
        e.replace(std::move(full)); e.setTool(rts::EditorTool::Paint); e.hardness=.5f;
        require(!e.apply({8.5f,8.5f}),"Paint stamp limit was bypassed"); e.endStroke();
        require(e.scenario().landscape.paint.size()==rts::Landscape::maximumPaintStamps && !e.undo(),"Rejected stamp changed history");
    });
    test("Editor separates cosmetic placement and ground-air spawn occupancy", [&] {
        const auto defs=rts::Definitions::load(assets/"data/catalog.json");
        rts::WorldEditor e({rts::Map(24,24),{1,1},{4,3},{}},worldAssets,defs);
        const auto choose=[&](rts::EditorTool tool,const std::string& id) {e.setTool(tool); const auto list=e.choices(); for(size_t i=0;i<list.size();++i) if(list[i].id==id)e.choice=i;};
        choose(rts::EditorTool::Unit,"human.peacemaker"); require(e.apply({8.5f,8.5f}),"Soldier placement failed"); e.endStroke();
        require(!e.apply({8.5f,8.5f}),"Two ground spawns accepted in one cell"); e.endStroke();
        choose(rts::EditorTool::Unit,"human.flying_soldier"); require(e.apply({8.5f,8.5f}),"Air spawn blocked by ground unit"); e.endStroke();
        choose(rts::EditorTool::Environment,"tree"); require(!e.apply({8.5f,8.5f}),"Tree placed through ground spawn"); e.endStroke();
        choose(rts::EditorTool::Decoration,"small_bush"); require(e.apply({8.153f,8.867f}),"Cosmetic placement rejected on occupied ground"); e.endStroke();
        require(e.scenario().landscape.decorations[0].position==rts::Vec2{8.153f,8.867f}&&e.scenario().map.occupancy({8,8})==0&&!e.scenario().map.blocksVision({8,8}),"Cosmetic object became gameplay blocker");
        choose(rts::EditorTool::Environment,"tree"); require(e.apply({10.5f,10.5f}),"Tree placement failed"); e.endStroke();
        require(e.scenario().map.blocksVision({10,10})&&!e.scenario().map.walkable({10,10}),"Gameplay definition lost collision or sight");
        e.setTool(rts::EditorTool::Remove); require(e.apply({10.5f,10.5f}),"Editor erase failed"); e.endStroke();
        require(!e.scenario().map.blocksVision({10,10})&&e.undo()&&e.scenario().map.blocksVision({10,10}),"Undo did not rebuild sight mask");
        e.validateForPlay();
    });
    test("Editor authors heights water and three-lane ramps with valid traversal", [&] {
        const auto defs=rts::Definitions::load(assets/"data/catalog.json");
        rts::Scenario s{rts::Map(24,24),{1,1},{4,3},{}};
        for(int y=0;y<24;++y) for(int x=12;x<24;++x) s.map.at({x,y}).height=1;
        rts::WorldEditor e(s,worldAssets,defs); e.setTool(rts::EditorTool::Ramp); e.direction=0;
        require(e.apply({11.5f,10.5f}),"Wide ramp placement failed"); e.endStroke();
        for(int y=9;y<=11;++y) require(e.scenario().map.canStep({11,y},{12,y}),"Ramp lane not traversable");
        require(e.scenario().map.canStep({11,9},{11,10}),"Wide ramp lanes disconnected");
        e.setTool(rts::EditorTool::Height); e.height=-1; e.radius=1.5f; require(e.apply({7.5f,17.5f}),"Lowering ground failed"); e.endStroke();
        e.setTool(rts::EditorTool::Surface); e.choice=1; require(e.apply({7.5f,17.5f}),"Water painting failed"); e.endStroke();
        require(e.scenario().map.at({7,17}).height==-1&&e.scenario().map.at({7,17}).surface==rts::Surface::ShallowWater,"Water depth changed elevation");
        require(e.undo()&&e.scenario().map.at({7,17}).surface==rts::Surface::Land&&e.scenario().map.at({7,17}).height==-1,"Undo surface edit damaged height");
        e.validateForPlay();
    });
    test("Ground art preserves source proportions with optional isometric projection", [&] {
        require(!worldAssets.material("grass").isometric && !worldAssets.material("dark_grass").isometric &&
            !worldAssets.material("sunny_hills.meadow").isometric && !worldAssets.material("sunny_hills.dry_grass").isometric &&
            worldAssets.material("sunny_hills.cobblestone").isometric, "Grass projection overrides affected other materials");
        const rts::GroundTextureProjection flat(12, 1536, false);
        const auto flatSize = flat.project({1536, 1024});
        require(flatSize == rts::Vec2{12, 8} && flat.unproject(flatSize) == rts::Vec2{1536, 1024},
            "Unprojected grass was rotated, compressed or stretched");
        const rts::GroundTextureProjection projection(8, 1536);
        const auto right = projection.project({1536, 0}), down = projection.project({0, 1024});
        require(std::abs(right.x - 2 * right.y) < .0001f && std::abs(down.x + 2 * down.y) < .0001f,
            "Ground texture axes lost the 2:1 slope");
        require(std::abs(right.x / -down.x - 1.5f) < .0001f, "Rectangular texture was stretched to a square");
        for (const rts::Vec2 source : {rts::Vec2{0, 0}, {1536, 1024}, {-245, 1800}, {87.25f, -32.5f}}) {
            const auto restored = projection.unproject(projection.project(source));
            require(std::hypot(restored.x - source.x, restored.y - source.y) < .001f,
                "Base brush and paint sampling use different projections");
        }
    });
    test("Projected paint wraps negative texture coordinates and repeats across chunk boundaries", [] {
        // This repeat size gives source-pixel axes (1, .5) and (-1, .5).
        rts::MaterialPixels material{4, 2, 4 * std::sqrt(2.0f), {}};
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x)
            material.pixels.push_back(0xff000000u | (uint32_t(x * 64) << 16) | uint32_t(y * 128));
        rts::Landscape landscape;
        landscape.paint = {{"pattern", {8, 8}, 16, 1, 1, false}};
        rts::TerrainPaint canvas;
        const auto image = [&](const std::string&) -> const rts::MaterialPixels& { return material; };
        canvas.update(landscape, 16, 16, image);
        const auto sample = [&](int x, int y) {
            return canvas.chunks().at({x / 256, y / 256}).pixels[(y % 256) * 256 + x % 256];
        };
        const auto close = [](uint32_t a, uint32_t b) {
            for (int shift : {0, 8, 16, 24})
                if (std::abs(int((a >> shift) & 255) - int((b >> shift) & 255)) > 1) return false;
            return true;
        };
        // World (4.015625, 1.015625) maps to source (3.0234375, -.9921875).
        // Wrapping and linear filtering yield red 162, blue 65, fully opaque.
        require(close(sample(128, 32), 0xffa20041), "Negative projected coordinates sampled the wrong texels");
        for (int y = 247; y <= 263; ++y) for (int x = 247; x <= 263; ++x) {
            const auto color = sample(x, y);
            require(close(color, sample(x + 128, y + 64)), "Horizontal source repeat drifted across paint chunks");
            require(close(color, sample(x - 64, y + 32)), "Vertical source repeat lost rectangular aspect or wrapping");
        }
    });
    test("Sparse terrain paint blends and erases across chunk boundaries and rebuilds after undo", [] {
        const rts::MaterialPixels red{1,1,4,{0xffff0000}},blue{1,1,4,{0xff0000ff}};
        const auto image=[&](const std::string& id)->const rts::MaterialPixels& {return id=="red"?red:blue;};
        rts::Landscape l; l.paint={{"red",{7.95f,7.95f},1.25f,1,.5f,false}};
        rts::TerrainPaint canvas; canvas.update(l,24,24,image);
        require(canvas.chunks().size()==4,"Paint did not cross four chunk corners");
        const auto sample=[&](int x,int y) { return canvas.chunks().at({x/rts::TerrainPaint::chunkPixels,y/rts::TerrainPaint::chunkPixels}).pixels[(y%rts::TerrainPaint::chunkPixels)*rts::TerrainPaint::chunkPixels+x%rts::TerrainPaint::chunkPixels]; };
        const auto original=sample(255,255); require((original>>24)==255&&sample(256,255)==original,"Chunk boundary left a seam");
        const auto revision=canvas.chunks().begin()->second.revision;
        require(!canvas.update(l,24,24,image)&&canvas.chunks().begin()->second.revision==revision,"Unchanged paint was recomputed");
        l.paint.push_back({"blue",{7.95f,7.95f},1,.5f,1,false}); canvas.update(l,24,24,image);
        const auto mix=sample(255,255); require(((mix>>16)&255)>100&&(mix&255)>100,"Second texture did not blend over first");
        l.paint.push_back({"red",{7.95f,7.95f},1,1,1,true}); canvas.update(l,24,24,image); require(sample(255,255)==0,"Eraser did not reveal base");
        l.paint.resize(1); require(canvas.update(l,24,24,image)&&sample(255,255)==original,"Undo did not restore cached paint");
        l.paint.clear(); canvas.update(l,24,24,image); require(canvas.chunks().empty(),"New blank map retained old paint");
    });
    test("Free brush picking follows ramps in all directions without snapping", [] {
        for(auto d:{rts::Cell{1,0},{-1,0},{0,1},{0,-1}}) for(float zoom:{.4f,1.0f,1.8f}) {
            rts::Map map(8,8); map.at({3,3}).ramp=d; map.at(rts::Cell{3,3}+d).height=1;
            const rts::WorldView view{{137,-81},zoom}; const rts::Vec2 p{3.173f,3.782f};
            const auto picked=map.pickPosition(view.project(p,map.surfaceHeight({3,3},p)),view);
            require(picked&&std::hypot(picked->x-p.x,picked->y-p.y)<.0001f,"Free placement drifted on a slope");
        }
    });
    test("World asset catalog can grow and rejects escaping paths and cosmetic collision", [&] {
        const auto root=worldPaths.writable(L"catalog-test/world/catalog.json").parent_path().parent_path();
        const rts::Paths fixture(root,root/"user");
        std::filesystem::copy_file(assets/"sprites/worker.png",root/"image.png",std::filesystem::copy_options::overwrite_existing);
        writeMap(root/"world/catalog.json",{{"materialFiles",{"world/materials.json"}},{"objectFiles",{"world/objects.json"}},{"remainsFile","world/remains.json"},{"resourcesFile","world/resources.json"}});
        std::ifstream resourceFile(assets/"world/resources.json"); auto resources=Json::parse(resourceFile);
        for (auto& icon : resources["icons"]) icon="image.png";
        for (auto& crystal : resources["crystals"]) crystal["image"]="image.png";
        writeMap(root/"world/resources.json",resources);
        Json bones={{"image","image.png"},{"scale",.5},{"variants",Json::array({{{"source",{0,0,16,16}},{"anchor",{8,12}}}})}};
        writeMap(root/"world/remains.json",{{"bones",bones}});
        nlohmann::json materials={{{"id","custom"},{"name","Своя текстура"},{"image","image.png"},{"repeatCells",8}}};
        nlohmann::json object={{"id","custom_tree"},{"name","Своя порода"},{"gameplay",true},{"kind","tree"},{"footprint",{1,1}},{"collision",{true}},{"health",250},{"blocksVision",true},
            {"sprite",{{"image","image.png"},{"source",{0,0,16,16}},{"size",{128,160}},{"anchor",{.5,.9}}}}};
        writeMap(root/"world/materials.json",materials); writeMap(root/"world/objects.json",nlohmann::json::array({object}));
        auto loaded=rts::WorldAssets::load(fixture); require(loaded.instantiate("custom_tree",77,{5,5}).maximumHitPoints==250,"Custom parameters did not reach instances");
        object["id"]="custom_bush"; object["gameplay"]=false; object["collision"]={false}; object["blocksVision"]=false;
        writeMap(root/"world/objects.json",nlohmann::json::array({object})); loaded=rts::WorldAssets::load(fixture);
        require(!loaded.object("custom_bush").gameplay,"New cosmetic definition did not load");
        object["collision"]={true}; writeMap(root/"world/objects.json",nlohmann::json::array({object})); mustThrow([&]{rts::WorldAssets::load(fixture);});
        object["collision"]={false}; object["sprite"]["image"]="../outside.png"; writeMap(root/"world/objects.json",nlohmann::json::array({object})); mustThrow([&]{rts::WorldAssets::load(fixture);});
        object["sprite"]["image"]="image.png"; writeMap(root/"world/objects.json",Json::array({object}));
        for (const auto& path : {"../bones.png", "C:/bones.png"}) {
            bones["image"]=path; writeMap(root/"world/remains.json",{{"bones",bones}}); mustThrow([&]{rts::WorldAssets::load(fixture);});
        }
        bones["image"]="image.png"; bones["variants"][0]["source"][2]=0;
        writeMap(root/"world/remains.json",{{"bones",bones}}); mustThrow([&]{rts::WorldAssets::load(fixture);});
    });
}
}
