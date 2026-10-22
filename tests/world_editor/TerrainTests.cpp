#include "WorldTerrainFiles.h"
#include "TerrainMappingCodec.h"
#include "WorldObjectCandidate.h"
#include "WorldModelCatalog.h"
#include "WorldMapCatalog.h"
#include "WorldTerrainUI.h"
#include "WorldEditorUI.h"
#include "WorldEditorPolicy.h"
#include "WorldFileIo.h"
#include "imgui.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <chrono>
#include <cmath>
using namespace Silver::WorldEditor;
namespace fs=std::filesystem;
int passed=0;
void Check(bool value,const char* name){if(!value)throw std::runtime_error(name);++passed;std::cout<<"PASS "<<name<<std::endl;}
template<class F>void Reject(F&& action,const char* name){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}Check(rejected,name);}
TerrainAttributeFile Attributes(AttributeEncoding encoding=AttributeEncoding::Encrypted16)
{
    TerrainAttributeFile file;file.Encoding=encoding;file.Cells.assign(TerrainCells,0);file.Cells[123*256+135]=5;return file;
}
WorldTerrainDocument Document()
{
    WorldTerrainDocument doc;doc.Load(Attributes(),std::vector<std::uint8_t>(TerrainCells,50),123*256+135);return doc;
}
TerrainMappingFile Mapping()
{
    TerrainMappingFile file;file.Map=1;
    std::fill(file.Layer1.begin(),file.Layer1.end(),5);
    std::fill(file.Layer2.begin(),file.Layer2.end(),255);
    std::fill(file.Alpha.begin(),file.Alpha.end(),0);
    return file;
}
WorldTerrainDocument MaterialDocument()
{
    WorldTerrainDocument doc;doc.Load(Attributes(),std::vector<std::uint8_t>(TerrainCells,50),Mapping(),123*256+135);return doc;
}
void CodecTests(const fs::path& fixtures)
{
    for(auto encoding:{AttributeEncoding::Raw8,AttributeEncoding::Raw16,AttributeEncoding::Encrypted8,AttributeEncoding::Encrypted16})
    {
        auto file=Attributes(encoding);const bool wide=encoding==AttributeEncoding::Raw16||encoding==AttributeEncoding::Encrypted16;
        for(int i=0;i<TerrainCells;++i)file.Cells[i]=static_cast<std::uint16_t>(i&(wide?65535:255));
        auto bytes=TerrainAttributeCodec::Encode(file);auto decoded=TerrainAttributeCodec::Decode(bytes);
        Check(file.Cells==decoded.Cells&&decoded.Encoding==encoding,"all ATT cells / encoding round-trip");
        Check(TerrainAttributeCodec::Encode(decoded)==bytes,"ATT round-trip byte-for-byte");
        bytes.pop_back();Reject([&]{TerrainAttributeCodec::Decode(bytes);},"truncated ATT rejected");
    }
    auto raw=Attributes(AttributeEncoding::Raw8);raw.Cells[0]=256;
    Reject([&]{TerrainAttributeCodec::Encode(raw);},"8-bit ATT refuses lossy high flags");
    raw=Attributes();raw.Encoding=static_cast<AttributeEncoding>(99);
    Reject([&]{TerrainAttributeCodec::Encode(raw);},"unknown encoding rejected");
    for(const char* leaf:{"EncTerrain1.att","Terrain.att"})
    {const auto bytes=WorldObjectCandidate::Read(fixtures/leaf);Check(TerrainAttributeCodec::Encode(TerrainAttributeCodec::Decode(bytes))==bytes,"real Lorencia ATT DIFF=0");}
    const auto bytes=WorldObjectCandidate::Read(fixtures/"TerrainHeight.OZB");
    const auto height=TerrainHeightFile::DecodeOzb(bytes);
    Check(height.Encode(height.Pixels())==bytes,"real Lorencia OZB DIFF=0 including opaque header");
    auto pixels=height.Pixels();pixels[120*256+100]^=1;const auto changed=height.Encode(pixels);
    Check(changed[1082+120*256+100]!=bytes[1082+120*256+100],"height uses verified loader offset and map axes");
    Check(std::equal(bytes.begin(),bytes.begin()+1082,changed.begin()),"height preserves wrapper and opaque header");
    auto truncated=bytes;truncated.pop_back();Reject([&]{TerrainHeightFile::DecodeOzb(truncated);},"truncated OZB rejected");
    const auto bmp=height.ToBitmap(pixels);
    Check(bmp.size()==66614&&bmp[0]=='B'&&bmp[1]=='M',"portable indexed BMP preview generated");
    Check(std::equal(pixels.begin(),pixels.end(),bmp.begin()+1078),"BMP preview contains exact edited samples");
}
void BrushTests()
{
    auto doc=Document();const auto before=doc.Heights();
    TerrainBrush brush;brush.Radius=2;brush.Strength=4;
    doc.Begin(brush);doc.Stamp(100,120);Check(doc.ChangedInStroke()==13,"circular radius two covers thirteen cells");
    doc.Stamp(100,120);Check(doc.Heights()[120*256+100]==54,"same cell is not raised repeatedly inside one stroke");
    Check(doc.Dirty()&&doc.Active(),"preview is dirty");Reject([&]{doc.MarkSaved();},"cannot save unfinished stroke");
    Check(doc.Commit()&&doc.Undo()&&doc.Heights()==before,"single undo restores complete height stroke");
    Check(!doc.Dirty()&&doc.Redo(),"undo to baseline is clean; redo works");doc.MarkSaved();Check(!doc.Dirty(),"saved terrain clean");
    doc.Undo();Check(doc.Dirty(),"undo away from saved state dirty");doc.Redo();Check(!doc.Dirty(),"redo saved state clean");
    doc=Document();brush.Radius=0;doc.Begin(brush);doc.Stamp(10,20);doc.Stamp(30,20);doc.Commit();
    bool continuous=true;for(int x=10;x<=30;++x)continuous=continuous&&doc.Heights()[20*256+x]==54;
    Check(continuous,"fast drag fills intermediate cells without gaps");Check(doc.Undo()&&doc.Heights()==before,"drag is one undo transaction");
    doc.Begin(brush);doc.Stamp(255,255);doc.Cancel();Check(doc.Heights()==before&&!doc.Dirty(),"cancel restores edge stroke");
    brush.Radius=2;doc.Begin(brush);doc.Stamp(0,0);doc.Commit();Check(doc.Heights()[255]==50&&doc.Heights()[255*256]==50,"brush clips instead of wrapping at map edge");
    doc.Undo();brush.Radius=0;brush.Mode=TerrainBrushMode::Lower;brush.Strength=64;doc.Begin(brush);doc.Stamp(20,30);doc.Commit();Check(doc.Heights()[30*256+20]==0,"lower clamps to zero without byte overflow");
    doc.Undo();brush.Mode=TerrainBrushMode::Flatten;brush.FlattenHeight=255;doc.Begin(brush);doc.Stamp(20,30);doc.Commit();
    brush.Mode=TerrainBrushMode::Raise;doc.Begin(brush);doc.Stamp(20,30);doc.Commit();Check(doc.Heights()[30*256+20]==255,"raise clamps at format maximum");
    doc=Document();auto attrs=Attributes();attrs.Cells[120*256+100]=0x4004;doc.Load(attrs,before,123*256+135);
    brush.Mode=TerrainBrushMode::AddFlags;brush.Radius=0;brush.Flags=1;doc.Begin(brush);doc.Stamp(100,120);doc.Commit();
    Check(doc.Flags(100,120)==0x4005,"Safe Zone preserves existing block and unknown high bits");
    brush.Mode=TerrainBrushMode::RemoveFlags;doc.Begin(brush);doc.Stamp(100,120);doc.Commit();Check(doc.Flags(100,120)==0x4004,"erase only chosen zone flag");
    brush.Flags=5;doc.Begin(brush);doc.Stamp(135,123);doc.Commit();Check(doc.Flags(135,123)==5,"client control cell cannot be erased");
    brush.Flags=128;Reject([&]{doc.Begin(brush);},"client-incompatible CameraUp flag rejected");
    brush.Flags=1;brush.Radius=17;Reject([&]{doc.Begin(brush);},"excessive radius rejected");
    brush.Radius=0;brush.Mode=static_cast<TerrainBrushMode>(100);Reject([&]{doc.Begin(brush);},"unknown tool rejected");
    auto heights=before;heights[100*256+100]=140;doc.Load(Attributes(),heights);
    brush.Mode=TerrainBrushMode::Smooth;brush.Radius=1;doc.Begin(brush);doc.Stamp(100,100);doc.Commit();
    Check(doc.Heights()[100*256+100]==60&&doc.Heights()[100*256+99]==60,"smoothing samples immutable pre-stroke grid");
    doc.Undo();Check(doc.Heights()==heights,"smooth undo exact");
    heights.assign(TerrainCells,50);heights[100*256+100]=100;doc.Load(Attributes(),heights);
    brush.Mode=TerrainBrushMode::Erode;brush.Radius=0;brush.Strength=10;doc.Begin(brush);doc.Stamp(100,100);doc.Commit();
    Check(doc.Heights()[100*256+100]==90,"erosion lowers a peak toward neighbor average by configured strength");
    Check(doc.Undo()&&doc.Heights()==heights,"erosion undo restores exact height grid");

    doc=Document();brush.Mode=TerrainBrushMode::Raise;brush.Radius=2;brush.Strength=10;brush.Falloff=1.0f;
    doc.Begin(brush);doc.Stamp(100,100);doc.Commit();
    Check(doc.Heights()[100*256+100]==60,"falloff keeps full strength at brush center");
    Check(doc.Heights()[100*256+102]==50,"full falloff reaches zero strength at brush edge");
    Check(doc.Heights()[100*256+101]>50&&doc.Heights()[100*256+101]<60,"falloff interpolates intermediate brush strength");
    Check(doc.Undo()&&doc.Heights()==before,"falloff stroke undo restores exact terrain");

    heights.assign(TerrainCells,50);heights[10*TerrainSide+10]=40;heights[10*TerrainSide+20]=100;
    doc.Load(Attributes(),heights);
    Check(doc.Ramp(10,10,20,10,0),"ramp between two terrain anchors commits");
    Check(doc.Heights()[10*TerrainSide+15]==70,"ramp interpolates midpoint height");
    Check(doc.Heights()[10*TerrainSide+10]==40&&doc.Heights()[10*TerrainSide+20]==100,
        "ramp preserves endpoint heights");
    Check(doc.Undo()&&doc.Heights()==heights,"ramp is one exact undo transaction");
    Reject([&]{doc.Ramp(10,10,10,10,0);},"ramp rejects identical endpoints");
}
void MaterialBrushTests()
{
    auto doc=MaterialDocument();const auto before=*doc.Mapping();
    TerrainBrush brush;brush.Mode=TerrainBrushMode::PaintLayer1;brush.Radius=0;brush.Material=7;
    doc.Begin(brush);doc.Stamp(100,120);doc.Commit();
    Check(doc.Mapping()->Layer1[120*TerrainSide+100]==7,"material base brush changes Layer1");
    Check(doc.Undo()&&*doc.Mapping()==before,"material undo restores both terrain layers and alpha");
    Check(doc.Redo()&&doc.Mapping()->Layer1[120*TerrainSide+100]==7,"material redo reapplies Layer1");
    brush.Mode=TerrainBrushMode::BlendMaterial;brush.Material=9;brush.BlendAlpha=200;
    doc.Begin(brush);doc.Stamp(100,120);doc.Commit();
    Check(doc.Mapping()->Layer2[120*TerrainSide+100]==9&&doc.Mapping()->Alpha[120*TerrainSide+100]==200,
        "blend brush changes Layer2 and alpha together");
    const auto blended=*doc.Mapping();
    brush.Mode=TerrainBrushMode::EraseBlend;doc.Begin(brush);doc.Stamp(100,120);doc.Commit();
    Check(doc.Mapping()->Layer2[120*TerrainSide+100]==255&&doc.Mapping()->Alpha[120*TerrainSide+100]==0,
        "erase blend restores runtime neutral layer state");
    Check(doc.Undo()&&*doc.Mapping()==blended,"undo restores erased blend exactly");
    brush.Mode=TerrainBrushMode::PaintLayer2;brush.Material=256;Reject([&]{doc.Begin(brush);},"material index above byte range rejected");
    auto noMap=Document();brush.Material=1;Reject([&]{noMap.Begin(brush);},"material brush requires runtime MAP");

    auto fillMapping=Mapping();
    for(int y=0;y<TerrainSide;++y)for(int x=128;x<TerrainSide;++x)fillMapping.Layer1[y*TerrainSide+x]=6;
    WorldTerrainDocument fillDoc;fillDoc.Load(Attributes(),std::vector<std::uint8_t>(TerrainCells,50),fillMapping);
    const auto fillBefore=*fillDoc.Mapping();
    brush.Mode=TerrainBrushMode::FillLayer1;brush.Material=9;brush.Radius=0;
    fillDoc.Begin(brush);fillDoc.Stamp(10,10);fillDoc.Commit();
    Check(fillDoc.Mapping()->Layer1[10*TerrainSide+10]==9
        && fillDoc.Mapping()->Layer1[10*TerrainSide+200]==6,
        "material fill replaces only the contiguous source region");
    Check(fillDoc.Undo()&&*fillDoc.Mapping()==fillBefore,"material fill is one exact undo transaction");
}
void WriteBytes(const fs::path& path,const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!out)throw std::runtime_error("fixture write failed");
}
void CandidateTests(const fs::path& fixtures,const fs::path& output)
{
    const auto world=output/"source/World1";fs::create_directories(world);
    for(const char* leaf:{"EncTerrain1.att","EncTerrain1.map","Terrain.att","TerrainHeight.OZB","EncTerrain1.obj"})fs::copy_file(fixtures/leaf,world/leaf);
    const auto loaded=TerrainAttributeCodec::Decode(WorldObjectCandidate::Read(world/"EncTerrain1.att"));
    WorldTerrainFiles files(world,1,loaded.Cells);auto doc=files.Open();WorldObjectCandidate candidate(world/"EncTerrain1.obj",output/"candidates");
    const auto baseline=doc.Heights();TerrainBrush brush;brush.Radius=1;doc.Begin(brush);doc.Stamp(100,120);doc.Commit();
    brush.Mode=TerrainBrushMode::AddFlags;brush.Flags=1;doc.Begin(brush);doc.Stamp(100,120);doc.Commit();
    brush.Mode=TerrainBrushMode::BlendMaterial;brush.Material=9;brush.BlendAlpha=200;doc.Begin(brush);doc.Stamp(100,120);doc.Commit();
    auto assets=files.Export(doc);const auto saved=candidate.Save(candidate.Baseline(),assets);
    Check(fs::exists(saved.parent_path()/"EncTerrain1.att")&&fs::exists(saved.parent_path()/"EncTerrain1.map")&&fs::exists(saved.parent_path()/"TerrainHeight.OZB"),"save includes objects, zones, materials and heights in one version");
    const auto decoded=TerrainAttributeCodec::Decode(WorldObjectCandidate::Read(saved.parent_path()/"EncTerrain1.att"));
    Check(decoded.Cells==doc.Attributes().Cells,"saved client ATT matches edited zone grid");
    const auto h=TerrainHeightFile::DecodeOzb(WorldObjectCandidate::Read(saved.parent_path()/"TerrainHeight.OZB"));
    Check(h.Pixels()==doc.Heights(),"saved OZB matches sculpted samples");
    const auto savedMapping=TerrainMappingCodec::Decode(WorldObjectCandidate::Read(saved.parent_path()/"EncTerrain1.map"));
    Check(savedMapping==*doc.Mapping(),"saved MAP matches edited material layers and alpha");
    const auto server=TerrainAttributeCodec::Decode(WorldObjectCandidate::Read(saved.parent_path()/"Terrain.att"));
    Check((server.Cells[120*256+100]&1)!=0,"raw ATT counterpart includes edited Safe Zone without applying to SQL");
    Check(WorldObjectCandidate::Read(world/"TerrainHeight.OZB")==WorldObjectCandidate::Read(fixtures/"TerrainHeight.OZB"),"original height file untouched");
    const auto second=candidate.Save(candidate.Baseline(),assets);Check(second!=saved&&fs::exists(saved),"every save preserves previous candidate");
    auto tamper=WorldObjectCandidate::Read(world/"EncTerrain1.att");tamper[16]^=1;WriteBytes(world/"EncTerrain1.att",tamper);
    Reject([&]{candidate.Save(candidate.Baseline(),assets);},"external ATT modification blocks whole candidate");
    fs::copy_file(fixtures/"EncTerrain1.att",world/"EncTerrain1.att",fs::copy_options::overwrite_existing);
    auto bad=assets;bad[0].Name="../escape.att";Reject([&]{candidate.Save(candidate.Baseline(),bad);},"path traversal sidecar rejected");
    bad=assets;bad.push_back(assets[0]);Reject([&]{candidate.Save(candidate.Baseline(),bad);},"excess or duplicate sidecars rejected");
    Check(!fs::exists(output/"escape.att"),"invalid save wrote no outside file");
}

void CandidateReloadTests(const fs::path& fixtures, const fs::path& output)
{
    const auto world=output/"reload-source/World1";fs::create_directories(world);
    for(const char* leaf:{"EncTerrain1.att","EncTerrain1.map","Terrain.att","TerrainHeight.OZB","EncTerrain1.obj"})
        fs::copy_file(fixtures/leaf,world/leaf);
    const auto loaded=TerrainAttributeCodec::Decode(WorldObjectCandidate::Read(world/"EncTerrain1.att"));
    WorldTerrainFiles files(world,1,loaded.Cells);auto terrain=files.Open();
    WorldObjectCandidate candidate(world/"EncTerrain1.obj",output/"reload-candidates");
    WorldObjectDocument objects;objects.Load(candidate.Baseline());
    auto moved=*objects.Find(1);moved.Position[0]+=20.0f;
    objects.Set(1,moved,"relocate tree or scenery object");
    Check(objects.Undo()&&objects.Redo(),"object movement undo/redo available before joint save");
    TerrainBrush brush;brush.Radius=0;brush.Mode=TerrainBrushMode::Raise;
    terrain.Begin(brush);terrain.Stamp(100,120);terrain.Commit();
    brush.Mode=TerrainBrushMode::AddFlags;brush.Flags=1;
    terrain.Begin(brush);terrain.Stamp(100,120);terrain.Commit();
    const auto saved=candidate.Save(objects.Export(),files.Export(terrain));
    WorldObjectCandidate reopened(saved,output/"second-generation");
    const auto reopenedLoaded=TerrainAttributeCodec::Decode(WorldObjectCandidate::Read(saved.parent_path()/"EncTerrain1.att"));
    WorldTerrainFiles reopenedFiles(saved.parent_path(),1,reopenedLoaded.Cells);auto reopenedTerrain=reopenedFiles.Open();
    Check(reopened.Baseline().Records==objects.Export().Records,"joint save reload restores moved objects exactly");
    Check(reopenedTerrain.Heights()==terrain.Heights(),"joint save reload restores sculpted terrain exactly");
    Check(reopenedTerrain.Attributes().Cells==terrain.Attributes().Cells,"joint save reload restores Safe Zone flags exactly");
    Check(reopenedTerrain.Mapping()&&*reopenedTerrain.Mapping()==*terrain.Mapping(),"joint save reload restores material MAP exactly");
    for(const char* leaf:{"EncTerrain1.att","EncTerrain1.map","Terrain.att","TerrainHeight.OZB","EncTerrain1.obj"})
        Check(WorldObjectCandidate::Read(world/leaf)==WorldObjectCandidate::Read(fixtures/leaf),"joint save left its source file unchanged");
    Check(IsBackgroundEditorRun("1","1")&&!IsBackgroundEditorRun("","1")&&!IsBackgroundEditorRun("1","0"),
          "hidden diagnostics require explicit native-editor environment flags");
}

void LongCandidateTests(const fs::path& fixtures, const fs::path& output)
{
    const auto workspace=output/std::string(100,'a')/std::string(100,'b')/"candidate-versions";
    Check(workspace.wstring().size()>260,"long-path regression exceeds traditional Windows limit");
    WorldObjectCandidate candidate(fixtures/"EncTerrain1.obj",workspace);
    const auto loaded=TerrainAttributeCodec::Decode(WorldObjectCandidate::Read(fixtures/"EncTerrain1.att"));
    WorldTerrainFiles files(fixtures,1,loaded.Cells);auto document=files.Open();
    const auto saved=candidate.Save(candidate.Baseline(),files.Export(document));
    Check(fs::exists(StreamPath(saved)),"candidate directory promotion works beyond 260 characters");
    const auto reopenedLoaded=TerrainAttributeCodec::Decode(WorldObjectCandidate::Read(saved.parent_path()/"EncTerrain1.att"));
    WorldTerrainFiles reopened(saved.parent_path(),1,reopenedLoaded.Cells);auto after=reopened.Open();
    Check(after.Heights()==document.Heights(),"long-path candidate heights reload exactly");
    Check(after.Attributes().Cells==document.Attributes().Cells,"long-path candidate attributes reload exactly");
    Check(fs::exists(StreamPath(saved.parent_path()/"Terrain.att")),"long-path raw attribute sidecar is preserved");
    Check(fs::exists(StreamPath(saved.parent_path()/"EncTerrain1.map")),"long-path material MAP sidecar is preserved");
}

void CatalogTests(const fs::path& output)
{
    const auto root=output/"Data";
    for(const char* folder:{"Object1","Object3","ObjectInvalid","Player"})fs::create_directories(root/folder);
    WriteBytes(root/"Object1/Tree01.bmd",{1});WriteBytes(root/"Object1/House01.BMD",{2});WriteBytes(root/"Object3/Tree02.bmd",{3});
    WriteBytes(root/"ObjectInvalid/Other.bmd",{4});WriteBytes(root/"Player/Player.bmd",{5});WriteBytes(root/"Object1/virus.exe",{6});
    WorldModelCatalog catalog;catalog.Scan(root);
    Check(catalog.Assets().size()==4&&catalog.FolderCount()==3,"catalog includes ObjectN and Player while excluding arbitrary directories");
    Check(WorldModelCatalog::Category("tReE03")=="Arboles"&&WorldModelCatalog::Category("House01")=="Edificios","human categories are case-insensitive");
    Check(!WorldModelCatalog::IsObjectFolder("Object../")&&!WorldModelCatalog::IsObjectFolder("Object1_backup"),"non-map folder names rejected");
    Check(std::all_of(catalog.Assets().begin(),catalog.Assets().end(),[](const auto& a){return a.RelativePath.find("..") == std::string::npos;}),"catalog paths remain relative to allowed Data folder");
}
void MapCatalogTests(const fs::path& output)
{
    const auto root=output/"MapData";
    const auto addWorld=[&](int world)
    {
        const auto folder=root/("World"+std::to_string(world));fs::create_directories(folder);
        const auto stem="EncTerrain"+std::to_string(world);
        WriteBytes(folder/(stem+".obj"),{1});
        WriteBytes(folder/(stem+".att"),{1});
        WriteBytes(folder/(stem+".map"),{1});
        WriteBytes(folder/"TerrainHeight.OZB",{1});
    };
    for(const int world:{1,2,12,19,25,31,47,75})addWorld(world);
    const auto maps=WorldMapCatalog::Discover(root);
    const auto has=[&](int map,int world)
    {
        return std::any_of(maps.begin(),maps.end(),[&](const auto& entry){return entry.MapId==map&&entry.WorldNumber==world;});
    };
    Check(has(0,1)&&has(1,2)&&has(11,12)&&has(17,12)&&has(52,12)
        &&has(18,19)&&has(23,19)&&has(53,19)
        &&has(24,25)&&has(29,25)&&has(36,25)
        &&has(30,31)&&has(45,47)&&has(50,47),
        "map catalog exposes logical aliases over shared MU worlds");
    Check(WorldMapCatalog::PhysicalWorldNumber(32)==10
        && WorldMapCatalog::PhysicalWorldNumber(36)==25
        && WorldMapCatalog::PhysicalWorldNumber(52)==12
        && WorldMapCatalog::PhysicalWorldNumber(53)==19
        && WorldMapCatalog::PhysicalWorldNumber(50)==47,
        "map catalog mirrors shared world resolution");
    Check(!std::any_of(maps.begin(),maps.end(),[](const auto& entry){return entry.WorldNumber==75;}),
        "login and character scenes stay outside editable map list");
}

class FakeScene:public IWorldEditorScene
{
public:
    WorldEditorCamera Camera()const override
    {
        const float v[3][4]{{1,0,0,-10000},{0,1,0,-12000},{0,0,1,-1000}};
        return WorldEditorCamera::FromClient(v,0.001f,0.001f,640,360,1280,720,10,10000,1280,720);
    }
    ObjectId Pick(float,float)override{return 1;}
    bool Placement(float,float,std::array<float,3>& p)override{p={10010,12010,75};return true;}
    bool Bounds(ObjectId,std::array<std::array<float,3>,8>&)const override{return false;}
    const std::vector<WorldPaletteItem>& Palette()const override{return palette;}
    void Synchronize(const WorldObjectDocument&)override{}
    std::vector<WorldPaletteItem> palette{{1,1,"Tree01",{1,{10000,12000,0},{0,0,0},1},"Arboles",{}}};
};
void UiTests()
{
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1280,720};io.DeltaTime=1.0f/60;
    unsigned char* pixels=nullptr;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    auto document=Document();const auto before=document.Heights();FakeScene scene;WorldTerrainUI ui;
    auto frame=[&]{ImGui::NewFrame();const auto save=ui.Draw(document,scene);ImGui::Render();return save;};
    Check(!frame()&&ImGui::GetDrawData()->Valid,"terrain and zone tool windows produce real ImGui draw data");frame();
    io.AddMousePosEvent(900,600);io.AddMouseButtonEvent(0,true);frame();
    Check(document.Active()&&document.Heights()[120*256+100]==54,"real UI pointer press starts visible height brush");
    io.AddMouseButtonEvent(0,false);frame();Check(!document.Active()&&document.CanUndo(),"UI pointer release commits one terrain action");
    io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_Z,true);frame();io.AddKeyEvent(ImGuiKey_Z,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
    Check(document.Heights()==before,"native Ctrl-Z restores all height samples");
    io.AddMouseButtonEvent(0,true);frame();io.AddKeyEvent(ImGuiKey_Escape,true);frame();
    io.AddKeyEvent(ImGuiKey_Escape,false);io.AddMouseButtonEvent(0,false);frame();
    Check(!document.Active()&&document.Heights()==before,"native Escape cancels height preview");
    io.AddMouseButtonEvent(0,true);frame();io.AddFocusEvent(false);frame();io.AddMouseButtonEvent(0,false);io.AddFocusEvent(true);frame();
    Check(!document.Active()&&document.Heights()==before,"losing focus cancels instead of committing partial brush");
    io.AddMousePosEvent(25,230);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    Check(document.Heights()==before,"clicking tool panel does not paint through UI");
    WorldObjectDocument objects;objects.Load({ObjectEncoding::MapEncrypted,0,0,{scene.palette[0].Prototype}});WorldEditorUI objectUi;
    ImGui::NewFrame();objectUi.Draw(objects,scene);ImGui::Render();
    Check(ImGui::GetDrawData()->Valid&&ImGui::GetDrawData()->TotalVtxCount>0,"visual model palette produces draw vertices");
    ImGui::DestroyContext();
}

void TestTerrainMappingCodec(const fs::path& fixtures)
{
    std::ifstream input(fixtures/"EncTerrain1.map",std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    Check(!bytes.empty(),"EncTerrain1.map fixture exists");
    Check(bytes.size()==TerrainMappingSerializedSize,"EncTerrain1.map has canonical serialized size");
    const auto decoded=TerrainMappingCodec::Decode(bytes);
    Check(sizeof(TerrainMappingFile)<256,"TerrainMappingFile keeps material layers off the thread stack");
    Check(decoded.Version==0&&decoded.Map==1,"MapFileDecrypt returns expected header");
    Check(decoded.Layer1[0]==5&&decoded.Layer2[0]==255&&decoded.Alpha[0]==0,
        "MapFileDecrypt preserves Layer1, Layer2 and Alpha offsets");
    Check(TerrainMappingCodec::Encode(decoded)==bytes,"EncTerrain1.map exact roundtrip");

}
void TestLegacyTerrainMappingRejected(const fs::path& fixtures)
{
    std::ifstream legacyInput(fixtures/"Terrain.map",std::ios::binary);
    std::vector<std::uint8_t> legacy((std::istreambuf_iterator<char>(legacyInput)),{});
    Check(legacy.size()==TerrainMappingSerializedSize-1,"legacy Terrain.map keeps the observed 196609-byte layout");
    Reject([&]{TerrainMappingCodec::Decode(legacy);},"legacy Terrain.map is not accepted as client EncTerrain input");
}
int main(int argc,char** argv)
{
    try
    {
        if(argc!=2)throw std::runtime_error("Fixture directory required");
        const auto output=fs::current_path()/("evidence-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(output);
        CodecTests(argv[1]);TestTerrainMappingCodec(argv[1]);TestLegacyTerrainMappingRejected(argv[1]);BrushTests();MaterialBrushTests();CandidateTests(argv[1],output);CandidateReloadTests(argv[1],output);LongCandidateTests(argv[1],output);CatalogTests(output);MapCatalogTests(output);UiTests();
        std::cout<<"ALL TERRAIN / PALETTE TESTS PASSED: "<<passed<<std::endl;return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<std::endl;return 1;}
}
