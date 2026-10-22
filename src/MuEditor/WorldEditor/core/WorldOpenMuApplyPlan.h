#pragma once
#include "WorldMonsterSpawnDocument.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldOpenMuSpawnPatch
{
    SpawnId Id=NoSpawn;
    WorldMonsterSpawn Expected;
    WorldMonsterSpawn Desired;
    bool operator==(const WorldOpenMuSpawnPatch&) const=default;
};
class WorldOpenMuApplyPlan
{
public:
    static constexpr const char* Schema="silver-world-editor-openmu-apply-plan-v1";
    int MapNumber=-1;
    std::string BaselineSha256;
    std::string CandidateSha256;
    std::vector<WorldOpenMuSpawnPatch> SpawnPatches;
    static WorldOpenMuApplyPlan Create(const WorldMonsterSpawnDocument& document,std::string baselineSha256,std::string candidateSha256);
    std::vector<std::uint8_t> Encode() const;
    static WorldOpenMuApplyPlan Decode(std::span<const std::uint8_t> bytes);
    bool ValidateAgainst(const WorldMonsterSpawnDocument& current,std::string_view baselineSha256) const;
    WorldOpenMuApplyPlan Inverse() const;
};
}
