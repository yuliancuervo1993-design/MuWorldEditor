#pragma once
#include "WorldEffectDocument.h"
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldAiLightCommand{EffectId Id=NoEffect;WorldPointLight Expected,Desired;};
struct WorldAiRuntimeEffectCommand{EffectId Id=NoEffect;WorldRuntimeEffect Expected,Desired;};
struct WorldAiEffectBatch
{
 static constexpr const char* Schema="silver-world-editor-ai-effect-batch-v1";
 std::string RequestId,Safety="CANDIDATE_ONLY; NO_AUTO_APPLY";int MapId=-1,WorldNumber=-1;
 std::vector<WorldAiLightCommand> Lights;std::vector<WorldAiRuntimeEffectCommand> RuntimeEffects;
 static WorldAiEffectBatch Decode(std::span<const std::uint8_t>);
 bool Validate(const WorldEffectDocument&,std::string&)const;
 bool Apply(WorldEffectDocument&,std::string&)const;
};
}
