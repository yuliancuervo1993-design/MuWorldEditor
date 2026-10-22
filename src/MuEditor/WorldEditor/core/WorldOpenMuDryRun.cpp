#include "WorldOpenMuDryRun.h"
namespace Silver::WorldEditor
{
WorldOpenMuDryRunReport WorldOpenMuDryRun::Evaluate(const WorldOpenMuApplyPlan& plan,const WorldMonsterSpawnDocument& live,std::string_view liveBaselineSha256)
{
    WorldOpenMuDryRunReport report;
    if(!live.Loaded()||live.MapNumber()!=plan.MapNumber||liveBaselineSha256!=plan.BaselineSha256)
    {
        for(const auto& patch:plan.SpawnPatches){report.Items.push_back({patch.Id,WorldOpenMuDryRunStatus::Conflict,"Baseline OpenMU/mapa no coincide con Apply Plan."});++report.Conflicts;}
        return report;
    }
    for(const auto& patch:plan.SpawnPatches)
    {
        const auto* current=live.FindBaseline(patch.Id);
        if(!current){report.Items.push_back({patch.Id,WorldOpenMuDryRunStatus::Missing,"Spawn persistente no existe en baseline actual."});++report.Missing;continue;}
        if(!(*current==patch.Expected)){report.Items.push_back({patch.Id,WorldOpenMuDryRunStatus::Conflict,"Spawn actual difiere del estado Expected."});++report.Conflicts;continue;}
        report.Items.push_back({patch.Id,WorldOpenMuDryRunStatus::WouldUpdate,"Precondicion Expected satisfecha; no se ejecuto escritura."});++report.WouldUpdate;
    }
    return report;
}
}
