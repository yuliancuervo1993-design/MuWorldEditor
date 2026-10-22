#pragma once
#include "WorldOpenMuApplyPlan.h"
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
enum class WorldOpenMuDryRunStatus { WouldUpdate, Conflict, Missing };
struct WorldOpenMuDryRunItem { SpawnId Id=NoSpawn; WorldOpenMuDryRunStatus Status=WorldOpenMuDryRunStatus::Missing; std::string Detail; };
struct WorldOpenMuDryRunReport
{
    std::vector<WorldOpenMuDryRunItem> Items;
    std::size_t WouldUpdate=0,Conflicts=0,Missing=0;
    bool Safe() const { return Conflicts==0&&Missing==0; }
};
class WorldOpenMuDryRun
{
public:
    static WorldOpenMuDryRunReport Evaluate(const WorldOpenMuApplyPlan& plan,const WorldMonsterSpawnDocument& live,std::string_view liveBaselineSha256);
};
}
