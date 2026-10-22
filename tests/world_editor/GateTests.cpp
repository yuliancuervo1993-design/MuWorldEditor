#include "WorldGateDocument.h"
#include "WorldGateCandidateFile.h"
#include "WorldHistoryCoordinator.h"
#include "json.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace Silver::WorldEditor;
using Json=nlohmann::json;
namespace
{
int passed=0;
void Check(bool condition,const char* label)
{ if(!condition)throw std::runtime_error(label); ++passed; }
template<class F> void Reject(F action,const char* label)
{ bool rejected=false; try{action();}catch(const std::exception&){rejected=true;} Check(rejected,label); }
std::vector<std::uint8_t> Bytes(const Json& value)
{ const auto text=value.dump();return {text.begin(),text.end()}; }
Json Baseline()
{
    return Json::parse(R"({"schema":"silver-openmu-gates-v1","mapNumber":0,"gates":[
      {"id":10,"number":17,"levelRequirement":20,"x1":120,"y1":130,"x2":121,"y2":131,
       "targetGateId":44,"targetMap":1,"targetX1":10,"targetY1":20,"targetX2":12,"targetY2":22,"targetDirection":3}]})");
}
void RejectMalformed(WorldGateDocument& document,const Json& source)
{
    const auto expected=document.Entries();
    for (const auto* field:{"id","number","levelRequirement","x1","y1","x2","y2","targetGateId",
        "targetMap","targetX1","targetY1","targetX2","targetY2","targetDirection"})
    {
        for (const Json invalid:{Json(-1),Json(1.5),Json(true),Json("12"),Json(nullptr)})
        {
            auto bad=source; bad["gates"][0][field]=invalid;
            Reject([&]{document.Load(Bytes(bad),0);},"invalid scalar accepted");
            Check(document.Entries()==expected,"invalid load changed the active document");
        }
    }
    for(const auto* field:{"x1","y1","x2","y2","targetX1","targetY1","targetX2","targetY2","targetDirection"})
    {auto bad=source;bad["gates"][0][field]=256;Reject([&]{document.Load(Bytes(bad),0);},"uint8 overflow accepted");}
    for(const auto* field:{"number","levelRequirement"})
    {auto bad=source;bad["gates"][0][field]=65536;Reject([&]{document.Load(Bytes(bad),0);},"int16 overflow accepted");}
    auto duplicate=source;duplicate["gates"].push_back(duplicate["gates"][0]);
    Reject([&]{document.Load(Bytes(duplicate),0);},"duplicate ID accepted");
    auto missing=source;missing["gates"][0].erase("targetGateId");
    Reject([&]{document.Load(Bytes(missing),0);},"missing target accepted");
}
void CandidateAndHistory(WorldGateDocument& document,const Json& source)
{
    const auto original=*document.Find(10); auto edited=original;edited.LevelRequirement=30;
    Check(document.SetCandidate(10,edited)&&document.Dirty(),"candidate edit");
    WorldHistoryCoordinator history;history.Record(WorldHistoryDomain::Gates,"Gate Candidate");
    Check(history.UndoEntry()->Domains.front()==WorldHistoryDomain::Gates,"global gate domain");
    Check(document.Undo()&&!document.Dirty()&&*document.Find(10)==original,"undo restores baseline");
    Check(document.Redo()&&*document.Find(10)==edited,"redo restores candidate");
    document.MarkSaved(); Check(!document.Dirty(),"saved state");
    Check(document.Undo()&&document.Dirty(),"undo after save is dirty");
    Check(document.Redo()&&!document.Dirty(),"redo restores saved state");
    auto invalid=edited;invalid.Number=99;
    Reject([&]{document.SetCandidate(10,invalid);},"candidate identity change accepted");
    const auto encoded=document.EncodeCandidate(); WorldGateDocument loaded;
    Reject([&]{loaded.LoadCandidate(encoded);},"candidate without baseline accepted");
    loaded.Load(Bytes(source),0);loaded.LoadCandidate(encoded);
    Check(loaded.Entries()==document.Entries()&&!loaded.Dirty(),"candidate round trip");
    auto stale=source;stale["gates"][0]["levelRequirement"]=21;loaded.Load(Bytes(stale),0);
    Reject([&]{loaded.LoadCandidate(encoded);},"stale baseline candidate accepted");
    Check(loaded.Find(10)->LevelRequirement==21,"stale candidate mutated baseline");
    loaded.Load(Bytes(source),0);
    auto bad=Json::parse(encoded.begin(),encoded.end());bad["gates"][0]["id"]=11;
    Reject([&]{loaded.LoadCandidate(Bytes(bad));},"foreign candidate ID accepted");
    bad["gates"]=Json::array();Reject([&]{loaded.LoadCandidate(Bytes(bad));},"candidate removed gate");
    document.Undo();edited.LevelRequirement=40;document.SetCandidate(10,edited);
    Check(!document.CanRedo(),"new edit failed to discard redo");
}
void AtomicCandidate(const WorldGateDocument& document)
{
    const auto root=std::filesystem::current_path()/("gate-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(std::filesystem::create_directory(root),"unique test directory");
    const auto path=root/WorldGateDocument::CandidateFileName;
    const auto bytes=document.EncodeCandidate();GateCandidateFile::Save(path,bytes);
    Check(std::filesystem::file_size(path)==bytes.size(),"candidate bytes saved");
    GateCandidateFile::Save(path,bytes);Check(std::filesystem::exists(path),"atomic replacement");
    {std::ofstream file(path.string()+".partial");file<<"unfinished";}
    Reject([&]{GateCandidateFile::Save(path,bytes);},"existing partial overwritten");
    std::ifstream file(path,std::ios::binary);const std::vector<std::uint8_t> actual((std::istreambuf_iterator<char>(file)),{});
    Check(actual==bytes,"failed save altered original");
    Reject([&]{GateCandidateFile::Save(root/"gates.json",bytes);},"baseline export overwritten");
}
}
int main()
{
    try
    {
        const auto source=Baseline(); WorldGateDocument document;document.Load(Bytes(source),0);
        Check(document.Loaded()&&document.Entries().size()==1,"load");
        Check(document.Find(10)->TargetMap==1&&document.Find(10)->TargetDirection==3,"fields");
        Reject([&]{document.Load(Bytes(source),1);},"map guard");
        RejectMalformed(document,source);CandidateAndHistory(document,source);AtomicCandidate(document);
        document.Clear();Check(!document.Loaded()&&!document.CanUndo()&&!document.Dirty(),"clear state");
        std::cout<<"WORLD_GATE_DOCUMENT_OK passed="<<passed<<"\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}
