#pragma once
#include <array>
#include <cstdint>

namespace Silver::WorldEditor
{
inline constexpr std::size_t MaximumObjects = 32767;
inline constexpr float MapEdge = 25600.0f;
inline constexpr float MinimumScale = 0.01f;
inline constexpr float MaximumScale = 100.0f;
using ObjectId = std::uint32_t;
inline constexpr ObjectId NoObject = 0;

struct WorldObjectRecord
{
    std::int16_t Type = 0;
    std::array<float, 3> Position{};
    std::array<float, 3> Angle{};
    float Scale = 1.0f;
    bool BaselineOnly = false;
    bool operator==(const WorldObjectRecord& other) const;
};

void ValidateRecord(const WorldObjectRecord& record);
int SpatialBlock(const WorldObjectRecord& record);
int LoadedSpatialBlock(const WorldObjectRecord& record);
}
