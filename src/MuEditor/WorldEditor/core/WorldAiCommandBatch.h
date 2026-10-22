#pragma once
#include "WorldObjectDocument.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldAiObjectCommand { ObjectId Id=NoObject; WorldObjectRecord Expected; WorldObjectRecord Desired; };
struct WorldAiCommandBatch
{
    static constexpr const char* Schema="silver-world-editor-ai-command-batch-v1";
    std::string RequestId;
    int MapId=-1,WorldNumber=-1;
    std::string Safety="CANDIDATE_ONLY; NO_AUTO_APPLY";
    std::vector<WorldAiObjectCommand> Commands;
    static WorldAiCommandBatch Decode(std::span<const std::uint8_t> bytes);
    std::vector<std::uint8_t> Encode() const;
    bool Validate(const WorldObjectDocument& document,std::string& error) const;
    bool Apply(WorldObjectDocument& document,std::string& error) const;
};
}
