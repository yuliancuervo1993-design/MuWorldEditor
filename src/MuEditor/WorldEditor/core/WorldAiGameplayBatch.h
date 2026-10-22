#pragma once
#include "WorldRegionDocument.h"
#include "WorldTriggerDocument.h"
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldAiRegionCommand { RegionId Id=NoRegion; WorldRegion Expected,Desired; };
struct WorldAiTriggerCommand { TriggerId Id=NoTrigger; WorldTrigger Expected,Desired; };
struct WorldAiGameplayBatch
{
 static constexpr const char* Schema="silver-world-editor-ai-gameplay-batch-v1";
 std::string RequestId,Safety="CANDIDATE_ONLY; NO_AUTO_APPLY";int MapId=-1;
 std::vector<WorldAiRegionCommand> Regions;std::vector<WorldAiTriggerCommand> Triggers;
 static WorldAiGameplayBatch Decode(std::span<const std::uint8_t>);
 bool Validate(const WorldRegionDocument&,const WorldTriggerDocument&,std::string&)const;
 bool Apply(WorldRegionDocument&,WorldTriggerDocument&,std::string&)const;
};
}
