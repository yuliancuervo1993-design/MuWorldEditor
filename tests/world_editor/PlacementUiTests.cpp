#include "WorldPlacementUI.h"
#include "WorldNpcLibraryUI.h"
#include "WorldViewportInput.h"
#include "WorldModelCatalog.h"
#include "WorldEditorUI.h"
#include "WorldTerrainUI.h"
#include "TerrainBrushFootprint.h"
#include "WorldMapNavigator.h"
#include "imgui.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Silver::WorldEditor;
namespace
{
int checks = 0;
void Check(bool value, const char* text)
{
    if (!value) throw std::runtime_error(text);
    ++checks; std::cout << "PASS " << text << '\n';
}
class Scene final : public IWorldEditorScene, public WorldPlacementSurface
{
public:
    bool ready = true;
    std::uint64_t identity = 1;
    std::optional<WorldObjectRecord> ghost;
    int focusCalls = 0;
    ObjectId focused = NoObject;
    int mapFocusCalls = 0;
    std::array<float,2> mapFocus{};
    std::array<float,3> cameraMap{10000,12000,1800};
    WorldEditorCamera Camera() const override
    {
        const float view[3][4]{{1,0,0,-10000},{0,1,0,-12000},{0,0,1,-1000}};
        return WorldEditorCamera::FromClient(view,0.001f,0.001f,640,360,1280,720,10,10000,1280,720);
    }
    ObjectId Pick(float,float) override { return 1; }
    bool Placement(float,float,std::array<float,3>& p) override { p={10010,12010,75}; return true; }
    bool Bounds(ObjectId,std::array<std::array<float,3>,8>&) const override { return false; }
    bool Focus(ObjectId id) override { ++focusCalls; focused=id; return true; }
    bool FocusMapPoint(float x,float y) override { ++mapFocusCalls; mapFocus={x,y}; return true; }
    bool CameraMapPosition(std::array<float,3>& position) const override { position=cameraMap; return true; }
    const std::vector<WorldPaletteItem>& Palette() const override { return palette; }
    void Synchronize(const WorldObjectDocument&) override {}
    const WorldPlacementSurface* PlacementSurface() const override { return this; }
    std::uint64_t SceneIdentity() const override { return identity; }
    void SetPlacementPreview(const std::optional<WorldObjectRecord>& record) override { ghost=record; }
    bool ModelReady(std::int16_t) const override { return ready; }
    bool HeightAt(float,float,float& h) const override { h=75; return true; }
    bool MinimumModelZ(const WorldObjectRecord& r,float& h) const override { h=-2*r.Scale; return true; }
    std::vector<WorldPaletteItem> palette{{1,1,"Arbol de prueba",{1,{10000,12000,0},{0,0,0},1},"Arboles",{}}};
};
struct Context
{
    Context()
    {
        ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.DisplaySize={1280,720};io.DeltaTime=1.0f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    }
    ~Context() { ImGui::DestroyContext(); }
};
WorldObjectDocument Objects(const Scene& scene)
{
    WorldObjectDocument doc;doc.Load({ObjectEncoding::MapEncrypted,0,0,{scene.palette[0].Prototype}});return doc;
}
void PlacementFlow()
{
    Context context;Scene scene;auto doc=Objects(scene);WorldPlacementUI ui;ObjectId selected=NoObject;
    auto& io=ImGui::GetIO();io.AddMousePosEvent(900,600);
    auto frame=[&] { ImGui::NewFrame();ui.Update(doc,scene,selected);ImGui::Render(); };
    const auto original=WorldObjectCodec::Encode(doc.Export());
    ui.Choose(scene.palette[0].Prototype,doc,scene);frame();
    Check(scene.ghost.has_value()&&!doc.Dirty(),"preview exists without changing document");
    Check(WorldObjectCodec::Encode(doc.Export())==original,"preview is excluded from save bytes");
    io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddMouseWheelEvent(0,1);frame();
    io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
    Check(scene.ghost&&std::abs(scene.ghost->Scale-1.1f)<0.0001f,"Ctrl+wheel scales preview before placement");
    io.AddKeyEvent(ImGuiKey_E,true);frame();io.AddKeyEvent(ImGuiKey_E,false);frame();
    Check(scene.ghost&&scene.ghost->Angle[2]==15,"E rotates preview exactly fifteen degrees");
    const auto preview=*scene.ghost;
    io.AddMouseButtonEvent(0,true);frame();frame();
    Check(doc.Entries().size()==2&&selected==2,"one press places one object and selects it");
    Check(*doc.Find(2)==preview,"placed transform equals last preview");
    Check(!scene.ghost&&!ui.Active(),"single placement clears ghost");
    io.AddMouseButtonEvent(0,false);frame();
    Check(doc.Undo()&&WorldObjectCodec::Encode(doc.Export())==original,"one undo restores exact map bytes");
    Check(doc.Redo()&&*doc.Find(2)==preview,"redo restores exact preview transform");
    ui.Choose(scene.palette[0].Prototype,doc,scene);frame();
    const auto count=doc.Entries().size();io.AddMouseButtonEvent(1,true);frame();
    Check(!ui.Active()&&!scene.ghost&&doc.Entries().size()==count,"right click cancels without another object");
    io.AddMouseButtonEvent(1,false);frame();
}
void PlacementGuards()
{
    Context context;Scene scene;auto doc=Objects(scene);WorldPlacementUI ui;ObjectId selected=NoObject;
    auto& io=ImGui::GetIO();io.AddMousePosEvent(900,600);
    auto frame=[&] {
        ImGui::NewFrame();ImGui::SetNextWindowPos({10,10});ImGui::SetNextWindowSize({300,300});
        ImGui::Begin("Panel de prueba");ImGui::TextUnformatted("No pintar debajo");ImGui::End();
        ui.Update(doc,scene,selected);ImGui::Render();
    };
    ui.Choose(scene.palette[0].Prototype,doc,scene);frame();frame();
    io.AddMousePosEvent(80,80);frame();io.AddMouseButtonEvent(0,true);frame();
    Check(!scene.ghost&&doc.Entries().size()==1,"panel hides ghost and blocks placement");
    io.AddMousePosEvent(900,600);frame();
    Check(doc.Entries().size()==1,"dragging a panel click into map does not place");
    io.AddMouseButtonEvent(0,false);frame();scene.ready=false;frame();
    Check(!scene.ghost,"unavailable model clears previously valid preview");
    io.AddMouseButtonEvent(0,true);frame();Check(doc.Entries().size()==1,"missing resource prevents insertion");
    io.AddMouseButtonEvent(0,false);frame();scene.ready=true;frame();
    io.AddFocusEvent(false);frame();
    Check(!ui.Active()&&!scene.ghost&&!doc.Dirty(),"focus loss cancels without data changes");
    io.AddFocusEvent(true);frame();ui.Choose(scene.palette[0].Prototype,doc,scene);frame();
    ++scene.identity;frame();Check(!ui.Active()&&!scene.ghost,"new scene invalidates ghost");
    ui.Choose(scene.palette[0].Prototype,doc,scene);frame();io.AddKeyEvent(ImGuiKey_Escape,true);frame();
    Check(!ui.Active()&&!scene.ghost&&!doc.Dirty(),"Escape cancels preview");
}
void BrushContracts()
{
    for (const auto center : {std::pair{0,0},std::pair{135,123},std::pair{255,255}})
        for (int radius=0;radius<=16;++radius)
        {
            bool matches=true;
            for(int y=0;y<256;++y)for(int x=0;x<256;++x)
            {
                const auto dx=static_cast<long long>(x-center.first),dy=static_cast<long long>(y-center.second);
                matches=matches&&(TerrainBrushFootprint::Contains(x,y,center.first,center.second,radius)==(dx*dx+dy*dy<=radius*radius));
            }
            Check(matches,"shared footprint equals independent circle oracle");
        }
    Check(!TerrainBrushFootprint::Contains(-1,0,0,0,3),"footprint clips map edge");
    Check(!TerrainBrushFootprint::Contains(0,0,0,0,17),"oversized footprint rejected");
    TerrainAttributeFile attributes;attributes.Encoding=AttributeEncoding::Encrypted16;attributes.Cells.assign(TerrainCells,0);
    WorldTerrainDocument doc;doc.Load(attributes,std::vector<std::uint8_t>(TerrainCells,50));
    TerrainBrush brush;brush.Radius=0;doc.Begin(brush);doc.Stamp(10,10);doc.BreakStrokePath();doc.Stamp(30,10);doc.Commit();
    Check(doc.Heights()[10*256+20]==50&&doc.Heights()[10*256+10]==54&&doc.Heights()[10*256+30]==54,
        "crossing a panel never paints a bridge between world points");
    Check(doc.Undo()&&doc.Heights()[10*256+10]==50,"interrupted path remains one undo");
    Context context;Scene scene;WorldTerrainUI ui;auto& io=ImGui::GetIO();io.AddMousePosEvent(900,600);
    auto frame=[&] { ImGui::NewFrame();ui.Draw(doc,scene);ImGui::Render(); };
    frame();frame();io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddMouseWheelEvent(0,1);frame();
    io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();io.AddMouseButtonEvent(0,true);frame();
    Check(doc.ChangedInStroke()==49,"Ctrl+wheel changes brush radius from three to four");
    io.AddMouseWheelEvent(0,5);frame();Check(doc.ChangedInStroke()==49,"active stroke freezes brush size");
    io.AddMouseButtonEvent(0,false);frame();Check(doc.Undo(),"wheel brush edit is undoable");
}
void MapNavigatorContracts()
{
    std::array<float,2> normalized{}, world{};
    Check(WorldMapNavigator::WorldToNormalized(12800.0f,6400.0f,normalized),
        "map navigator accepts an interior Lorencia point");
    Check(std::abs(normalized[0]-0.5f)<0.0001f && std::abs(normalized[1]-0.75f)<0.0001f,
        "map navigator keeps X and inverts Y for top-down canvas");
    Check(WorldMapNavigator::NormalizedToWorld(normalized[0],normalized[1],world),
        "map navigator converts canvas coordinates back to world");
    Check(std::abs(world[0]-12800.0f)<0.1f && std::abs(world[1]-6400.0f)<0.1f,
        "map navigator round-trip preserves world coordinates");
    Check(WorldMapNavigator::NormalizedToWorld(1.0f,0.0f,world)
        && world[0]<MapEdge && world[1]<MapEdge,
        "map navigator clamps visual far edge inside editable map");
    Check(!WorldMapNavigator::WorldToNormalized(-1.0f,0.0f,normalized)
        && !WorldMapNavigator::WorldToNormalized(MapEdge,0.0f,normalized),
        "map navigator rejects outside-map world positions");
    Check(!WorldMapNavigator::NormalizedToWorld(
        std::numeric_limits<float>::quiet_NaN(),0.5f,world),
        "map navigator rejects non-finite canvas coordinates");
}

void EditorPointerRegression()
{
    Context context;auto& io=ImGui::GetIO();
    io.AddMousePosEvent(900,400);io.AddMouseButtonEvent(ImGuiMouseButton_Middle,true);
    ImGui::NewFrame();
    auto pointer=ReadWorldCameraPointer(false);
    Check(pointer.Held&&pointer.X==900&&pointer.Y==400,"middle button reads independent editor input even with F10 right-look off");
    io.WantCaptureMouse=true;Check(!ReadWorldCameraPointer(true).Held,"camera cannot drag through a UI panel");
    io.WantCaptureMouse=false;io.AppFocusLost=true;Check(!ReadWorldCameraPointer(true).Held,"unfocused camera does not rotate");
    io.AppFocusLost=false;ImGui::Render();
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle,false);ImGui::NewFrame();
    Check(!ReadWorldCameraPointer(true).Held,"releasing middle ends camera drag");ImGui::Render();
    Check(WorldModelCatalog::IsGameplayEntityPath("Custom/Monster/BossRemake/Monster38.bmd"),"custom mobs are excluded from objects");
    Check(WorldModelCatalog::IsGameplayEntityPath("NPC/Smith.bmd"),"NPC models are excluded from objects");
    Check(!WorldModelCatalog::IsGameplayEntityPath("Object1/Tree01.bmd")&&!WorldModelCatalog::IsGameplayEntityPath("Item/Sword01.bmd"),"static objects and decorative items stay available");
}
void NpcPlacementFlow()
{
    Context context;Scene scene;WorldMonsterSpawnDocument doc;WorldNpcLibraryUI ui;SpawnId selected=NoSpawn;
    const std::string baseline=R"({"schema":"silver-openmu-monster-spawns-v2","mapNumber":0,"spawns":[]})";
    doc.Load(std::vector<std::uint8_t>{baseline.begin(),baseline.end()},0);
    WorldMonsterSpawn npc;npc.MonsterNumber=251;npc.Designation="Hanzo";npc.ObjectKind="PassiveNpc";
    npc.Quantity=1;npc.SpawnTrigger="Automatic";npc.Direction=8;
    auto& io=ImGui::GetIO();io.AddMousePosEvent(900,400);
    auto frame=[&] {ImGui::NewFrame();ui.Update(doc,scene,selected);ImGui::Render();};
    ui.Choose(npc);frame();frame();
    Check(ui.Placing()&&!doc.Dirty(),"NPC position preview does not change the server baseline");
    io.AddMouseButtonEvent(0,true);frame();frame();io.AddMouseButtonEvent(0,false);frame();
    const auto* placed=doc.Find(selected);
    Check(placed&&placed->MonsterNumber==251&&placed->X1==100&&placed->Y1==120&&placed->Direction==8,"NPC click places correct server definition on terrain coordinates");
    Check(!ui.Placing()&&doc.Entries().size()==1&&doc.Undo()&&doc.Entries().empty()&&doc.Redo(),"NPC held click inserts once and supports undo redo");
    ui.Choose(npc);io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    Check(doc.Entries().size()==1,"Escape cancels NPC placement without a new spawn");
}
void NpcRepeatGuards()
{
    Context context;Scene scene;WorldMonsterSpawnDocument doc;WorldNpcLibraryUI ui;SpawnId selected=NoSpawn;
    const std::string baseline=R"({"schema":"silver-openmu-monster-spawns-v2","mapNumber":0,"spawns":[]})";
    doc.Load(std::vector<std::uint8_t>{baseline.begin(),baseline.end()},0);
    WorldMonsterSpawn npc;npc.MonsterNumber=251;npc.Designation="Hanzo";npc.ObjectKind="PassiveNpc";
    npc.Quantity=2;npc.SpawnTrigger="Automatic";npc.Direction=8;
    auto& io=ImGui::GetIO();io.AddMousePosEvent(900,400);
    auto frame=[&]{ImGui::NewFrame();ui.Update(doc,scene,selected);ImGui::Render();};
    ui.SetRepeat(true);ui.Choose(npc);frame();
    io.AddKeyEvent(ImGuiKey_E,true);frame();io.AddKeyEvent(ImGuiKey_E,false);frame();
    io.AddMouseButtonEvent(0,true);frame();frame();io.AddMouseButtonEvent(0,false);frame();
    Check(ui.Placing()&&doc.Entries().size()==1&&doc.Find(selected)->Direction==1,"repeat places once per press and E wraps direction eight to one");
    io.AddKeyEvent(ImGuiKey_Q,true);frame();io.AddKeyEvent(ImGuiKey_Q,false);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    Check(doc.Entries().size()==2&&doc.Find(selected)->Direction==8&&doc.Find(selected)->Quantity==2,"repeat preserves quantity and Q wraps direction one to eight");
    Check(doc.Undo()&&doc.Entries().size()==1&&doc.Redo(),"each repeated spawn has independent undo redo");
    io.AddMouseButtonEvent(2,true);io.AddMouseButtonEvent(0,true);frame();
    Check(doc.Entries().size()==2,"camera middle drag cannot place a spawn");
    io.AddMouseButtonEvent(0,false);io.AddMouseButtonEvent(2,false);frame();
    ++scene.identity;frame();Check(!ui.Placing(),"NPC placement cancels on scene change");
    ui.SetRepeat(false);ui.Choose(npc);io.AddKeyEvent(ImGuiMod_Shift,true);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    Check(ui.Placing()&&doc.Entries().size()==3,"Shift keeps one-shot NPC tool active");
    io.AddKeyEvent(ImGuiMod_Shift,false);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    Check(!ui.Placing()&&doc.Entries().size()==4,"releasing Shift restores single placement");
}
void PaletteIntegration()
{
    Context context;Scene scene;auto doc=Objects(scene);WorldEditorUI ui;auto& io=ImGui::GetIO();
    // A1 reserves a bottom asset shelf; exercise placement over the central 3D viewport.
    io.AddMousePosEvent(900,400);ui.ChoosePrototype(scene.palette[0],doc);
    Check(ui.Selected()==NoObject&&!doc.Dirty(),"choosing palette never selects or edits the example object");
    auto frame=[&] { ImGui::NewFrame();const bool save=ui.Draw(doc,scene);ImGui::Render();return save; };
    frame();frame();
    Check(ui.Placing()&&scene.ghost&&!doc.Dirty(),"actual object panel publishes provisional scene preview");
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    Check(doc.Entries().size()==2&&ui.Selected()==2,"actual object panel confirms and selects one new copy");
    Check(!ui.Placing()&&!scene.ghost,"panel leaves placement mode after confirmation");
    io.AddKeyEvent(ImGuiKey_F,true);frame();io.AddKeyEvent(ImGuiKey_F,false);frame();
    Check(scene.focusCalls==1&&scene.focused==2,"F focuses the selected placed object through scene adapter");
    Check(doc.Undo()&&doc.Find(1)&&!doc.Find(2),"actual panel placement uses document undo");
}
}
int main()
{
    try
    {
        NpcRepeatGuards();NpcPlacementFlow();EditorPointerRegression();PlacementFlow();PlacementGuards();BrushContracts();MapNavigatorContracts();PaletteIntegration();
        std::cout<<"R3_UI_CHECKS passed="<<checks<<" failed=0\n";return 0;
    }
    catch(const std::exception& e) { std::cerr<<"R3_UI_FAILURE "<<e.what()<<'\n';return 1; }
}
