#pragma once
#include "WorldObjectCodec.h"
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
inline constexpr const char* SilverWorldStudioPlacementSchema = "silver-world-studio-placement-v1";
inline constexpr const char* SilverWorldStudioCandidateSchema = "silver-world-studio-candidate-v2";

struct SilverWorldStudioDiff { int Index=-1; WorldObjectRecord Before; WorldObjectRecord After; };

struct SilverWorldStudioPlacement
{
    int Index = -1;
    int Type = -1;
    int ModelSequence = -1;
    std::optional<std::string> Model;
    std::array<float, 3> Position{};
    std::array<float, 3> Rotation{};
    float Scale = 1.0f;
    std::optional<int> InstancesOfType;
};

struct SilverWorldStudioContract
{
    std::filesystem::path File;
    std::string Schema;
    bool ReadOnlySource = false;
    std::string Map;
    int MapNumber = -1;
    int MapWorld = -1;
    std::size_t PlacementCount = 0;
    std::size_t ResolvedModelCount = 0;
    std::vector<SilverWorldStudioPlacement> Placements;
};

class SilverWorldStudioBridge
{
public:
    static std::filesystem::path DefaultContractRoot();
    static std::optional<SilverWorldStudioContract> TryLoadForMap(
        const std::filesystem::path& contractRoot, int mapNumber, int mapWorld);
    static SilverWorldStudioContract Load(const std::filesystem::path& file);
    static WorldObjectFile Apply(const SilverWorldStudioContract& contract,
        const WorldObjectFile& baseline, int expectedMapNumber, int expectedMapWorld);
    static std::vector<std::uint8_t> EncodeCandidate(const SilverWorldStudioContract& contract,
        const WorldObjectFile& baseline, int expectedMapNumber, int expectedMapWorld);
    static WorldObjectFile DecodeCandidate(std::span<const std::uint8_t> bytes,
        const WorldObjectFile& baseline, int expectedMapNumber, int expectedMapWorld);
    static std::vector<SilverWorldStudioDiff> Diff(const SilverWorldStudioContract& contract,
        const WorldObjectFile& baseline, int expectedMapNumber, int expectedMapWorld);
};
}
