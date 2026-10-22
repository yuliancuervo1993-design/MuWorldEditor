#include "WorldNpcCatalog.h"
#include "json.hpp"
#include <stdexcept>
#include <unordered_set>
namespace Silver::WorldEditor
{
void WorldNpcCatalog::Load(std::span<const std::uint8_t> bytes)
{
    constexpr std::size_t MaximumBytes=2*1024*1024,MaximumDefinitions=10000;
    if(bytes.empty()||bytes.size()>MaximumBytes)throw std::invalid_argument("Catalogo NPC fuera de limites.");
    const auto root=nlohmann::json::parse(bytes.begin(),bytes.end());
    if(root.value("schema","")!="silver-world-editor-npc-catalog-v1" || root.value("readOnly","")!="on"
        || root.value("database","")!="silvercore_local" || root.value("serverPort",0)!=55432)
        throw std::invalid_argument("Catalogo no corresponde a Silver Core LOCAL.");
    const auto& definitions=root.at("definitions");
    if(!definitions.is_array()||definitions.empty()||definitions.size()>MaximumDefinitions)
        throw std::invalid_argument("Lista de NPC invalida.");
    std::vector<WorldMonsterSpawn> entries;std::unordered_set<int> numbers;
    for(const auto& row:definitions)
    {
        const int number=row.at("number").get<int>();
        if(number<0||number>32767||!numbers.insert(number).second)throw std::invalid_argument("ID NPC invalido o duplicado.");
        WorldMonsterSpawn value;value.MonsterNumber=static_cast<std::int16_t>(number);
        value.Designation=row.at("designation").get<std::string>();value.ObjectKind=row.at("objectKind").get<std::string>();
        value.RespawnDelayMilliseconds=row.at("respawnDelayMilliseconds").get<std::int64_t>();
        if(value.Designation.empty()||value.Designation.size()>128||value.ObjectKind.empty()
            ||value.ObjectKind.size()>128||value.RespawnDelayMilliseconds<0)throw std::invalid_argument("Definicion NPC invalida.");
        value.SpawnTrigger="Automatic";value.Quantity=1;entries.push_back(std::move(value));
    }
    exportedUtc_=root.at("exportedUtc").get<std::string>();entries_=std::move(entries);
}
}
