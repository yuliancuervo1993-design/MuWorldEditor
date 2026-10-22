#include "WorldMonsterSpawnDocument.h"
#include "WorldNpcCatalog.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace Silver::WorldEditor;

namespace
{
int passed = 0;
void Check(bool value, const char* name)
{
    if (!value) throw std::runtime_error(name);
    ++passed;
    std::cout << "PASS " << name << '\n';
}
template<class Error, class Action>
void Reject(Action action, const char* name)
{
    try { action(); }
    catch (const Error&) { Check(true, name); return; }
    throw std::runtime_error(name);
}
std::vector<std::uint8_t> Bytes(std::string text)
{
    return {text.begin(), text.end()};
}
}

void CheckActualExport(const std::filesystem::path& root)
{
    auto read=[](const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file),{}};};
    WorldNpcCatalog catalog;catalog.Load(read(root/"npc-catalog.json"));
    std::size_t maps=0,spawns=0;
    for(const auto& entry:std::filesystem::directory_iterator(root))
    {
        const auto name=entry.path().filename().string();if(!entry.is_directory()||!name.starts_with("Map"))continue;
        const int map=std::stoi(name.substr(3));WorldMonsterSpawnDocument document;
        const auto baseline=read(entry.path()/"monster-spawns.json");document.Load(baseline,map);
        spawns+=document.Entries().size();++maps;
        auto npc=catalog.Entries().front();npc.X1=npc.X2=120;npc.Y1=npc.Y2=140;npc.Direction=8;
        if(!document.MapVariants().empty())npc.MapDefinitionId=document.MapVariants().back().first;
        const auto id=document.AddCandidate(npc);const auto saved=document.EncodeCandidate();
        WorldMonsterSpawnDocument restored;restored.Load(baseline,map);restored.LoadCandidate(saved,map);
        Check(restored.Find(id)&&restored.Find(id)->Direction==8,"actual-map new spawn roundtrip preserves OpenMU direction");
        Check(restored.Find(id)->MapDefinitionId==npc.MapDefinitionId,"actual-map candidate preserves exact server variant");
    }
    std::cout<<"ACTUAL_EXPORT definitions="<<catalog.Entries().size()<<" maps="<<maps<<" spawns="<<spawns<<'\n';
}
int main(int argc,char** argv)
{
    try
    {
        if(argc==2)CheckActualExport(argv[1]);
        const auto json = Bytes(R"({
          "schema":"silver-openmu-monster-spawns-v2",
          "mapNumber":0,
          "spawns":[
            {"id":101,"persistentId":"11111111-1111-1111-1111-111111111111","monsterNumber":2,"designation":"Budge Dragon","objectKind":"Monster",
             "x1":120,"y1":130,"x2":125,"y2":135,"direction":3,"quantity":8,
             "spawnTrigger":"Automatic","waveNumber":0,"respawnDelayMilliseconds":10000},
            {"id":102,"persistentId":"22222222-2222-2222-2222-222222222222","monsterNumber":249,"designation":"Guard","objectKind":"Guard",
             "x1":140,"y1":141,"x2":140,"y2":141,"direction":1,"quantity":1,
             "spawnTrigger":"Automatic","waveNumber":0,"respawnDelayMilliseconds":0}
          ]})");
        WorldMonsterSpawnDocument document;
        document.Load(json, 0);
        Check(document.Loaded() && document.MapNumber() == 0, "spawn export loads expected map");
        Check(document.Entries().size() == 2, "spawn export loads all entries");
        Check(document.Find(101) && document.Find(101)->MonsterNumber == 2, "spawn lookup resolves monster number");
        Check(document.Find(101)->PersistentId=="11111111-1111-1111-1111-111111111111","spawn preserves persistent OpenMU GUID");
        Check(!document.Find(101)->IsPoint() && document.Find(102)->IsPoint(), "spawn point and area are distinguished");
        auto edit=*document.Find(101);edit.X1=121;edit.Y1=131;edit.Quantity=9;
        Check(document.SetCandidate(101,edit,"Move Spawn Area"),"spawn candidate edit");
        Check(document.Dirty()&&document.CanUndo()&&document.UndoLabel()=="Move Spawn Area","spawn candidate history");
        Check(document.ChangedIds().size()==1&&document.ChangedIds().front()==101,"spawn changed ids");
        Check(document.FindBaseline(101)&&document.FindBaseline(101)->X1==120,"spawn baseline preserved");
        const auto candidate=document.EncodeCandidate();
        Check(document.Undo()&&document.Find(101)->X1==120,"spawn candidate undo");
        Check(document.Redo()&&document.Find(101)->X1==121,"spawn candidate redo");
        WorldMonsterSpawnDocument candidateRestored;candidateRestored.Load(json,0);candidateRestored.LoadCandidate(candidate,0);
        Check(candidateRestored.Find(101)->X1==121&&candidateRestored.Find(101)->Quantity==9,"spawn candidate roundtrip");
        Check(!candidateRestored.Dirty(),"loaded spawn candidate clean");
        Reject<std::invalid_argument>([&] { document.Load(json, 1); }, "spawn export rejects another map");
        auto invalid = Bytes(R"({"schema":"silver-openmu-monster-spawns-v2","mapNumber":0,"spawns":[
          {"id":1,"persistentId":"33333333-3333-3333-3333-333333333333","monsterNumber":2,"designation":"Bad","objectKind":"Monster","x1":20,"y1":20,"x2":10,"y2":20,
           "direction":0,"quantity":1,"spawnTrigger":"Automatic","waveNumber":0,"respawnDelayMilliseconds":0}]})");
        Reject<std::invalid_argument>([&] { document.Load(invalid, 0); }, "spawn export rejects inverted area");
        WorldNpcCatalog catalog;
        catalog.Load(Bytes(R"({"schema":"silver-world-editor-npc-catalog-v1","readOnly":"on","database":"silvercore_local","serverPort":55432,"exportedUtc":"2026-09-27","definitions":[{"number":251,"designation":"Hanzo","objectKind":"PassiveNpc","respawnDelayMilliseconds":0}]})"));
        Check(catalog.Entries().size()==1&&catalog.Entries()[0].MonsterNumber==251,"actual server identity retained by NPC catalog");
        auto added=catalog.Entries()[0];added.X1=added.X2=120;added.Y1=added.Y2=140;
        const auto id=document.AddCandidate(added);
        Check(document.Find(id)&&!document.FindBaseline(id)&&document.Dirty(),"new NPC is candidate-only and has no fabricated server baseline");
        const auto withNpc=document.EncodeCandidate();
        WorldMonsterSpawnDocument loaded;loaded.Load(json,0);loaded.LoadCandidate(withNpc,0);
        Check(loaded.Find(id)&&loaded.Find(id)->MonsterNumber==251&&loaded.Find(id)->X1==120,"new NPC persists and reloads at the chosen position");
        Check(document.Undo()&&!document.Find(id),"undo removes newly placed NPC");
        Check(document.Redo()&&document.Find(id),"redo restores newly placed NPC");
        std::cout << "WORLD_MONSTER_SPAWN_DOCUMENT_OK passed=" << passed << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
