#pragma once
#include "WorldSilverHdRegistry.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldSilverHdPromotionPlan
{
    static constexpr const char* Schema="silver-hd-promotion-plan-v1";
    std::string LogicalKey,SourceRelativePath,SourceSha256,RuntimeRelativePath,RuntimeSha256;
    std::uintmax_t SourceBytes=0,RuntimeBytes=0;
    WorldSilverHdState SourceState=WorldSilverHdState::Unclassified;
    WorldSilverHdRuntimeState RuntimeState=WorldSilverHdRuntimeState::Unchecked;
    bool RequiresRuntimeReplacement=false;
    std::vector<std::uint8_t> Encode() const;
    static WorldSilverHdPromotionPlan Decode(std::span<const std::uint8_t> bytes);
};
class WorldSilverHdPromotion
{
public:
    static WorldSilverHdPromotionPlan Prepare(const WorldSilverHdRegistry& registry,const WorldSilverHdAsset& asset);
    static bool VerifySource(const WorldSilverHdRegistry& registry,const WorldSilverHdPromotionPlan& plan);
};
}
