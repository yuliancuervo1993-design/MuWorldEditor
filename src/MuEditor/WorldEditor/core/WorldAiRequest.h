#pragma once
#include "SilverWorldStudioBridge.h"
#include "WorldObjectDocument.h"
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
inline constexpr const char* SilverWorldEditorAiRequestSchema = "silver-world-editor-ai-request-v1";

enum class WorldAiWorkMode
{
    ImproveExisting,
    CreateNew,
    ReferenceGuided,
};

struct WorldAiRequest
{
    std::string RequestId;
    WorldAiWorkMode WorkMode = WorldAiWorkMode::ImproveExisting;
    int MapId = -1;
    int WorldNumber = -1;
    std::string MapName;
    ObjectId SelectedObject = NoObject;
    std::int16_t Type = -1;
    int ModelSequence = -1;
    std::array<float, 3> Position{};
    std::array<float, 3> Rotation{};
    float Scale = 1.0f;
    std::optional<std::string> ResolvedBmd;
    int ExpectedPlacements = 1;
    std::vector<std::string> VerifiedReferences;
    std::string Prompt;
    std::string Safety = "CANDIDATE_ONLY; NO_AUTO_APPLY";

    static std::string ModeName(WorldAiWorkMode mode);
    static WorldAiRequest FromSelection(
        int mapId, int worldNumber, const WorldObjectDocument& document, ObjectId selected,
        const SilverWorldStudioContract* bridge, WorldAiWorkMode mode, std::string prompt);
    static WorldAiRequest ForPlacement(
        int mapId, int worldNumber, std::string mapName, std::array<float, 3> position,
        const SilverWorldStudioContract* bridge, WorldAiWorkMode mode, std::string prompt);
    std::string ToJson() const;
};
}
