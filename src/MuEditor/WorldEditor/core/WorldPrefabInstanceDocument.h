#pragma once
#include "WorldObjectDocument.h"
#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldPrefabInstance{std::uint64_t Id=0;std::string PrefabName,PrefabSha256;std::vector<ObjectId> Objects;bool operator==(const WorldPrefabInstance&)const=default;};
class WorldPrefabInstanceDocument
{
public:
 static constexpr const char* Schema="silver-world-editor-prefab-instances-v1";static constexpr const char* FileName="prefab-instances.json";
 std::uint64_t Add(std::string name,std::string sha,std::span<const ObjectId> objects);
 const std::vector<WorldPrefabInstance>& Entries()const{return entries_;}
 void Prune(const WorldObjectDocument&);
 std::vector<std::uint8_t> Encode(int mapId)const;void Decode(std::span<const std::uint8_t>,int mapId);
private:std::vector<WorldPrefabInstance> entries_;std::uint64_t nextId_=1;
};
}
