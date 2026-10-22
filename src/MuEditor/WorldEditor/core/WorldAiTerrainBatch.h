#pragma once
#include "WorldTerrainDocument.h"
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldAiTerrainStroke { TerrainBrush Brush; int X=0,Y=0; };
struct WorldAiTerrainBatch
{
 static constexpr const char* Schema="silver-world-editor-ai-terrain-batch-v1";
 std::string RequestId,Safety="CANDIDATE_ONLY; NO_AUTO_APPLY";int MapId=-1;std::uint64_t ExpectedRevision=0;
 std::vector<WorldAiTerrainStroke> Strokes;
 static WorldAiTerrainBatch Decode(std::span<const std::uint8_t>);
 bool Validate(const WorldTerrainDocument&,int activeMap,std::string&)const;
 bool Apply(WorldTerrainDocument&,int activeMap,std::string&)const;
};
}
