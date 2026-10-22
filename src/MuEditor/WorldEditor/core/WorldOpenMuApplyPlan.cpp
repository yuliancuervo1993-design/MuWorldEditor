#include "WorldOpenMuApplyPlan.h"
#include "json.hpp"
#include <stdexcept>
#include <unordered_set>
namespace Silver::WorldEditor
{
namespace
{
using Json=nlohmann::json;constexpr std::size_t MaxBytes=2*1024*1024,MaxPatches=10000;
Json Spawn(const WorldMonsterSpawn& s){return {{"id",s.Id},{"persistentId",s.PersistentId},{"mapDefinitionId",s.MapDefinitionId},{"mapVariant",s.MapVariant},{"monsterNumber",s.MonsterNumber},{"designation",s.Designation},{"objectKind",s.ObjectKind},{"x1",s.X1},{"y1",s.Y1},{"x2",s.X2},{"y2",s.Y2},{"direction",s.Direction},{"quantity",s.Quantity},{"spawnTrigger",s.SpawnTrigger},{"waveNumber",s.WaveNumber},{"respawnDelayMilliseconds",s.RespawnDelayMilliseconds}};}
WorldMonsterSpawn Parse(const Json& j){WorldMonsterSpawn s;s.MapDefinitionId=j.value("mapDefinitionId",std::string{});s.MapVariant=j.value("mapVariant",std::string{});s.Id=j.value("id",NoSpawn);s.PersistentId=j.value("persistentId",std::string{});s.MonsterNumber=j.value("monsterNumber",static_cast<std::int16_t>(-1));s.Designation=j.value("designation",std::string{});s.ObjectKind=j.value("objectKind",std::string{});s.X1=j.value("x1",static_cast<std::uint8_t>(0));s.Y1=j.value("y1",static_cast<std::uint8_t>(0));s.X2=j.value("x2",static_cast<std::uint8_t>(0));s.Y2=j.value("y2",static_cast<std::uint8_t>(0));s.Direction=j.value("direction",static_cast<std::uint8_t>(0));s.Quantity=j.value("quantity",static_cast<std::int16_t>(0));s.SpawnTrigger=j.value("spawnTrigger",std::string{});s.WaveNumber=j.value("waveNumber",static_cast<std::uint8_t>(0));s.RespawnDelayMilliseconds=j.value("respawnDelayMilliseconds",static_cast<std::int64_t>(0));if(s.Id==NoSpawn||s.PersistentId.empty()||s.PersistentId.size()>64||s.MonsterNumber<0||s.X1>s.X2||s.Y1>s.Y2)throw std::invalid_argument("Apply Plan contiene Spawn invalido.");return s;}
}
WorldOpenMuApplyPlan WorldOpenMuApplyPlan::Create(const WorldMonsterSpawnDocument& document,std::string baselineSha256,std::string candidateSha256)
{
    if(!document.Loaded()||baselineSha256.size()!=64||candidateSha256.size()!=64)throw std::invalid_argument("Apply Plan requiere baseline/Candidate verificados.");
    WorldOpenMuApplyPlan plan;plan.MapNumber=document.MapNumber();plan.BaselineSha256=std::move(baselineSha256);plan.CandidateSha256=std::move(candidateSha256);
    for(const auto id:document.ChangedIds()){const auto* before=document.FindBaseline(id);const auto* after=document.Find(id);if(!before||!after)throw std::logic_error("Apply Plan no puede resolver Spawn modificado.");plan.SpawnPatches.push_back({id,*before,*after});}
    return plan;
}
std::vector<std::uint8_t> WorldOpenMuApplyPlan::Encode() const
{
    if(MapNumber<0||BaselineSha256.size()!=64||CandidateSha256.size()!=64||SpawnPatches.size()>MaxPatches)throw std::invalid_argument("Apply Plan invalido.");
    Json patches=Json::array();for(const auto& p:SpawnPatches)patches.push_back({{"id",p.Id},{"expected",Spawn(p.Expected)},{"desired",Spawn(p.Desired)}});
    Json root{{"schema",Schema},{"mapNumber",MapNumber},{"baselineSha256",BaselineSha256},{"candidateSha256",CandidateSha256},{"spawnPatches",patches}};auto text=root.dump(2)+"\n";if(text.size()>MaxBytes)throw std::length_error("Apply Plan excede limite.");return {text.begin(),text.end()};
}
WorldOpenMuApplyPlan WorldOpenMuApplyPlan::Decode(std::span<const std::uint8_t> bytes)
{
    if(bytes.empty()||bytes.size()>MaxBytes)throw std::invalid_argument("Apply Plan fuera de limites.");Json root;try{root=Json::parse(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}catch(const Json::exception&){throw std::invalid_argument("Apply Plan JSON invalido.");}
    if(root.value("schema","")!=Schema)throw std::invalid_argument("Apply Plan schema incompatible.");WorldOpenMuApplyPlan p;p.MapNumber=root.value("mapNumber",-1);p.BaselineSha256=root.value("baselineSha256",std::string{});p.CandidateSha256=root.value("candidateSha256",std::string{});
    if(p.MapNumber<0||p.BaselineSha256.size()!=64||p.CandidateSha256.size()!=64||!root.contains("spawnPatches")||!root.at("spawnPatches").is_array()||root.at("spawnPatches").size()>MaxPatches)throw std::invalid_argument("Apply Plan incompleto.");
    std::unordered_set<SpawnId> ids;for(const auto& j:root.at("spawnPatches")){WorldOpenMuSpawnPatch patch;patch.Id=j.value("id",NoSpawn);patch.Expected=Parse(j.at("expected"));patch.Desired=Parse(j.at("desired"));if(patch.Id==NoSpawn||patch.Expected.Id!=patch.Id||patch.Desired.Id!=patch.Id||patch.Expected.PersistentId!=patch.Desired.PersistentId||!ids.insert(patch.Id).second)throw std::invalid_argument("Apply Plan patch ID invalido/duplicado.");p.SpawnPatches.push_back(std::move(patch));}return p;
}
bool WorldOpenMuApplyPlan::ValidateAgainst(const WorldMonsterSpawnDocument& current,std::string_view baselineSha256) const
{
    if(!current.Loaded()||current.MapNumber()!=MapNumber||baselineSha256!=BaselineSha256)return false;
    for(const auto& patch:SpawnPatches){const auto* live=current.FindBaseline(patch.Id);if(!live||!(*live==patch.Expected))return false;}return true;
}
WorldOpenMuApplyPlan WorldOpenMuApplyPlan::Inverse() const
{
    WorldOpenMuApplyPlan inverse=*this;std::swap(inverse.BaselineSha256,inverse.CandidateSha256);for(auto& patch:inverse.SpawnPatches)std::swap(patch.Expected,patch.Desired);return inverse;
}
}
