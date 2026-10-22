#pragma once
#include "WorldMonsterSpawnDocument.h"
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldAiSpawnCommand { SpawnId Id=NoSpawn; WorldMonsterSpawn Expected,Desired; };
struct WorldAiSpawnBatch
{
 static constexpr const char* Schema="silver-world-editor-ai-spawn-batch-v1";
 std::string RequestId,Safety="CANDIDATE_ONLY; NO_AUTO_APPLY";int MapId=-1;
 std::vector<WorldAiSpawnCommand> Commands;
 static WorldAiSpawnBatch Decode(std::span<const std::uint8_t>);
 bool Validate(const WorldMonsterSpawnDocument&,std::string&)const;
 bool Apply(WorldMonsterSpawnDocument&,std::string&)const;
};
}
