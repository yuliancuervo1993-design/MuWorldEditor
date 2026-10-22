#include "WorldMonsterSpawnDocument.h"
#include "json.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>
namespace Silver::WorldEditor
{
namespace
{
using Json=nlohmann::json;constexpr std::size_t MaximumBytes=2*1024*1024,MaximumSpawns=10000,MaximumHistory=256;
std::string RequiredString(const Json& value,const char* name){if(!value.contains(name)||!value.at(name).is_string())throw std::invalid_argument(std::string("Monster spawn sin ")+name+".");auto result=value.at(name).get<std::string>();if(result.size()>128)throw std::invalid_argument("Texto de monster spawn demasiado largo.");return result;}
WorldMonsterSpawn ParseSpawn(const Json& value)
{
    if(!value.is_object())throw std::invalid_argument("MonsterSpawn invalido.");WorldMonsterSpawn s;
    s.MapDefinitionId=value.value("mapDefinitionId",std::string{});s.MapVariant=value.value("mapVariant",std::string{});
    s.Id=value.value("id",static_cast<SpawnId>(NoSpawn));s.PersistentId=value.value("persistentId",std::string{});s.MonsterNumber=value.value("monsterNumber",static_cast<std::int16_t>(-1));
    s.Designation=RequiredString(value,"designation");s.ObjectKind=RequiredString(value,"objectKind");
    s.X1=value.value("x1",static_cast<std::uint8_t>(0));s.Y1=value.value("y1",static_cast<std::uint8_t>(0));s.X2=value.value("x2",static_cast<std::uint8_t>(0));s.Y2=value.value("y2",static_cast<std::uint8_t>(0));
    s.Direction=value.value("direction",static_cast<std::uint8_t>(0));s.Quantity=value.value("quantity",static_cast<std::int16_t>(0));s.SpawnTrigger=RequiredString(value,"spawnTrigger");
    s.WaveNumber=value.value("waveNumber",static_cast<std::uint8_t>(0));s.RespawnDelayMilliseconds=value.value("respawnDelayMilliseconds",static_cast<std::int64_t>(0));return s;
}
Json EncodeSpawn(const WorldMonsterSpawn& s){return {{"id",s.Id},{"persistentId",s.PersistentId},{"mapDefinitionId",s.MapDefinitionId},{"mapVariant",s.MapVariant},{"monsterNumber",s.MonsterNumber},{"designation",s.Designation},{"objectKind",s.ObjectKind},{"x1",s.X1},{"y1",s.Y1},{"x2",s.X2},{"y2",s.Y2},{"direction",s.Direction},{"quantity",s.Quantity},{"spawnTrigger",s.SpawnTrigger},{"waveNumber",s.WaveNumber},{"respawnDelayMilliseconds",s.RespawnDelayMilliseconds}};}
}
void WorldMonsterSpawnDocument::Validate(const WorldMonsterSpawn& spawn)
{
    if (spawn.Id == NoSpawn || spawn.PersistentId.empty() || spawn.PersistentId.size() > 64 || spawn.MonsterNumber < 0 || spawn.X1 > spawn.X2 || spawn.Y1 > spawn.Y2
        || spawn.Quantity < 0 || spawn.Direction > 8 || spawn.RespawnDelayMilliseconds < 0
        || spawn.Designation.empty() || spawn.Designation.size() > 128
        || spawn.ObjectKind.empty() || spawn.ObjectKind.size() > 128
        || spawn.SpawnTrigger.empty() || spawn.SpawnTrigger.size() > 128)
        throw std::invalid_argument("MonsterSpawn contiene propiedades fuera de rango.");
}
void WorldMonsterSpawnDocument::Clear(){mapNumber_=-1;mapVariants_.clear();baseline_.clear();entries_.clear();history_.clear();cursor_=0;state_=nextState_=savedState_=0;}
void WorldMonsterSpawnDocument::Load(std::span<const std::uint8_t> bytes,int expectedMap)
{
    if(bytes.empty()||bytes.size()>MaximumBytes)throw std::invalid_argument("Export de MonsterSpawns fuera de limites.");Json root;
    try{root=Json::parse(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}catch(const Json::exception&){throw std::invalid_argument("Export de MonsterSpawns no contiene JSON valido.");}
    if(!root.is_object()||root.value("schema","")!=Schema)throw std::invalid_argument("Schema de MonsterSpawns no compatible.");const int map=root.value("mapNumber",-1);
    if(map!=expectedMap)throw std::invalid_argument("El export de MonsterSpawns pertenece a otro mapa.");
    if(!root.contains("spawns")||!root.at("spawns").is_array()||root.at("spawns").size()>MaximumSpawns)throw std::invalid_argument("Lista de MonsterSpawns invalida.");
    std::vector<WorldMonsterSpawn> parsed;std::unordered_set<SpawnId> ids;
    for(const auto& value:root.at("spawns")){auto s=ParseSpawn(value);Validate(s);if(!ids.insert(s.Id).second)throw std::invalid_argument("MonsterSpawn con ID vacio o duplicado.");parsed.push_back(std::move(s));}
    std::vector<std::pair<std::string,std::string>> variants;
    if(root.contains("variants"))for(const auto& variant:root.at("variants"))
        variants.emplace_back(variant.at("id").get<std::string>(),variant.at("name").get<std::string>());
    mapVariants_=std::move(variants);
    mapNumber_=map;baseline_=parsed;entries_=std::move(parsed);history_.clear();cursor_=0;state_=nextState_=savedState_=0;
}
void WorldMonsterSpawnDocument::LoadCandidate(std::span<const std::uint8_t> bytes,int expectedMap)
{
    if(!Loaded())throw std::logic_error("Candidate de MonsterSpawns requiere export OpenMU actual cargado.");
    if(bytes.empty()||bytes.size()>MaximumBytes)throw std::invalid_argument("Candidate MonsterSpawns fuera de limites.");Json root;
    try{root=Json::parse(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}catch(const Json::exception&){throw std::invalid_argument("Candidate MonsterSpawns JSON invalido.");}
    if(root.value("schema","")!=CandidateSchema||root.value("mapNumber",-1)!=expectedMap||expectedMap!=mapNumber_)throw std::invalid_argument("Candidate MonsterSpawns schema/mapa incompatible.");
    if(!root.contains("edits")||!root.at("edits").is_array()||root.at("edits").size()>MaximumSpawns)throw std::invalid_argument("Candidate MonsterSpawns edits invalidos.");
    entries_=baseline_;std::unordered_set<SpawnId> ids;
    for(const auto& value:root.at("edits")){auto changed=ParseSpawn(value);Validate(changed);if(!ids.insert(changed.Id).second)throw std::invalid_argument("Candidate MonsterSpawns ID duplicado.");auto it=std::find_if(entries_.begin(),entries_.end(),[&](const auto& x){return x.Id==changed.Id;});if(it==entries_.end())throw std::invalid_argument("Candidate MonsterSpawns referencia ID inexistente en export actual.");*it=std::move(changed);}
    if(root.contains("added"))
    {
        if(!root.at("added").is_array()||entries_.size()+root.at("added").size()>MaximumSpawns)throw std::invalid_argument("Demasiados spawns nuevos.");
        for(const auto& row:root.at("added"))
        {
            auto added=ParseSpawn(row);Validate(added);
            if(!added.PersistentId.starts_with("draft:")||Find(added.Id))throw std::invalid_argument("Alta NPC duplicada o ajena al borrador.");
            entries_.push_back(std::move(added));
        }
    }
    history_.clear();cursor_=0;state_=nextState_=savedState_=0;
}
std::vector<std::uint8_t> WorldMonsterSpawnDocument::EncodeCandidate() const
{
    if(!Loaded())throw std::logic_error("No hay MonsterSpawns OpenMU cargados.");Json edits=Json::array(),added=Json::array();
    for(const auto& current:entries_){auto base=std::find_if(baseline_.begin(),baseline_.end(),[&](const auto& x){return x.Id==current.Id;});if(base==baseline_.end()){Validate(current);added.push_back(EncodeSpawn(current));continue;}if(!(current==*base)){Validate(current);edits.push_back(EncodeSpawn(current));}}
    Json root{{"schema",CandidateSchema},{"mapNumber",mapNumber_},{"edits",edits},{"added",added}};auto text=root.dump(2)+"\n";if(text.size()>MaximumBytes)throw std::length_error("Candidate MonsterSpawns excede limite.");return {text.begin(),text.end()};
}
SpawnId WorldMonsterSpawnDocument::AddCandidate(const WorldMonsterSpawn& prototype)
{
    if(!Loaded()||entries_.size()>=MaximumSpawns)throw std::logic_error("Mapa sin baseline o limite de spawns alcanzado.");
    WorldMonsterSpawn added=prototype;SpawnId highest=0;
    for(const auto& entry:entries_)highest=std::max(highest,entry.Id);
    if(highest==UINT64_MAX)throw std::length_error("No quedan identificadores de spawn.");
    added.Id=highest+1;added.PersistentId="draft:"+std::to_string(mapNumber_)+":"+std::to_string(added.Id);
    if(!mapVariants_.empty())
    {
        const auto variant=std::find_if(mapVariants_.begin(),mapVariants_.end(),[&](const auto& v){return v.first==added.MapDefinitionId;});
        if(variant==mapVariants_.end())throw std::invalid_argument("Elegir la variante de mapa del servidor.");
        added.MapVariant=variant->second;
    }
    Validate(added);auto before=Capture();entries_.push_back(added);
    PushHistory(std::move(before),"Colocar NPC / Mob");return added.Id;
}
void WorldMonsterSpawnDocument::Restore(const Snapshot& s){entries_=s.Entries;}
void WorldMonsterSpawnDocument::PushHistory(Snapshot before,std::string label)
{
    if(cursor_<history_.size())history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());const auto beforeState=state_,afterState=++nextState_;
    history_.push_back({std::move(before),Capture(),std::move(label),beforeState,afterState});if(history_.size()>MaximumHistory)history_.erase(history_.begin());cursor_=history_.size();state_=history_.back().AfterState;++historySerial_;
}
bool WorldMonsterSpawnDocument::SetCandidate(SpawnId id,const WorldMonsterSpawn& value,std::string label)
{
    if(!Loaded())return false;auto it=std::find_if(entries_.begin(),entries_.end(),[id](const auto& x){return x.Id==id;});if(it==entries_.end())return false;WorldMonsterSpawn changed=value;changed.Id=id;Validate(changed);if(*it==changed)return false;auto before=Capture();*it=std::move(changed);PushHistory(std::move(before),std::move(label));return true;
}
bool WorldMonsterSpawnDocument::Undo(){if(!CanUndo())return false;const auto& e=history_[--cursor_];Restore(e.Before);state_=e.BeforeState;return true;}
bool WorldMonsterSpawnDocument::Redo(){if(!CanRedo())return false;const auto& e=history_[cursor_++];Restore(e.After);state_=e.AfterState;return true;}
std::string WorldMonsterSpawnDocument::UndoLabel() const{return CanUndo()?history_[cursor_-1].Label:std::string{};}
const WorldMonsterSpawn* WorldMonsterSpawnDocument::Find(SpawnId id) const{const auto found=std::find_if(entries_.begin(),entries_.end(),[id](const auto& s){return s.Id==id;});return found==entries_.end()?nullptr:&*found;}
const WorldMonsterSpawn* WorldMonsterSpawnDocument::FindBaseline(SpawnId id) const{const auto found=std::find_if(baseline_.begin(),baseline_.end(),[id](const auto& s){return s.Id==id;});return found==baseline_.end()?nullptr:&*found;}
std::vector<SpawnId> WorldMonsterSpawnDocument::ChangedIds() const
{
    std::vector<SpawnId> result;for(const auto& current:entries_){const auto* base=FindBaseline(current.Id);if(!base||!(current==*base))result.push_back(current.Id);}return result;
}
}
