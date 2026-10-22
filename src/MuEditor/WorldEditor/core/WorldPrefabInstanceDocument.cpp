#include "WorldPrefabInstanceDocument.h"
#include "json.hpp"
#include <algorithm>
#include <stdexcept>
namespace Silver::WorldEditor
{
std::uint64_t WorldPrefabInstanceDocument::Add(std::string n,std::string sha,std::span<const ObjectId>o){if(n.empty()||sha.size()!=64||o.empty())throw std::invalid_argument("Prefab instance metadata invalida.");WorldPrefabInstance x;x.Id=nextId_++;x.PrefabName=std::move(n);x.PrefabSha256=std::move(sha);x.Objects.assign(o.begin(),o.end());entries_.push_back(std::move(x));return entries_.back().Id;}
void WorldPrefabInstanceDocument::Prune(const WorldObjectDocument&d){for(auto&i:entries_)std::erase_if(i.Objects,[&](ObjectId id){return d.Find(id)==nullptr;});std::erase_if(entries_,[](const auto&i){return i.Objects.empty();});}
std::vector<std::uint8_t> WorldPrefabInstanceDocument::Encode(int map)const{nlohmann::json a=nlohmann::json::array();for(const auto&i:entries_)a.push_back({{"id",i.Id},{"prefab",i.PrefabName},{"sha256",i.PrefabSha256},{"objects",i.Objects}});nlohmann::json j{{"schema",Schema},{"mapId",map},{"instances",a}};auto s=j.dump(2)+"\n";return{s.begin(),s.end()};}
void WorldPrefabInstanceDocument::Decode(std::span<const std::uint8_t>b,int map){entries_.clear();nextId_=1;if(b.empty())return;auto j=nlohmann::json::parse(std::string(reinterpret_cast<const char*>(b.data()),b.size()));if(j.value("schema","")!=Schema||j.value("mapId",-1)!=map)throw std::invalid_argument("Prefab instances incompatible.");for(const auto&v:j.at("instances")){WorldPrefabInstance i;i.Id=v.value("id",0ull);i.PrefabName=v.value("prefab","");i.PrefabSha256=v.value("sha256","");i.Objects=v.value("objects",std::vector<ObjectId>{});if(!i.Id||i.PrefabName.empty()||i.PrefabSha256.size()!=64||i.Objects.empty())throw std::invalid_argument("Prefab instance corrupta.");nextId_=std::max(nextId_,i.Id+1);entries_.push_back(std::move(i));}}
}
