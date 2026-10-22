#pragma once
#include "WorldMonsterSpawnDocument.h"
#include <span>
namespace Silver::WorldEditor
{
class WorldNpcCatalog
{
public:
    void Load(std::span<const std::uint8_t> bytes);
    const std::vector<WorldMonsterSpawn>& Entries() const { return entries_; }
    const std::string& ExportedUtc() const { return exportedUtc_; }
private:
    std::vector<WorldMonsterSpawn> entries_;
    std::string exportedUtc_;
};
}
