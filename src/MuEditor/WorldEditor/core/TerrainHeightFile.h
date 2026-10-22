#pragma once
#include "TerrainAttributeCodec.h"
namespace Silver::WorldEditor
{
inline constexpr float HeightUnit = 1.5f;
class TerrainHeightFile
{
public:
    static bool SupportsLegacyEditing(std::span<const std::uint8_t> bytes);
    static TerrainHeightFile DecodeOzb(std::span<const std::uint8_t> bytes);
    const std::vector<std::uint8_t>& Pixels() const { return pixels_; }
    std::vector<std::uint8_t> Encode(std::span<const std::uint8_t> pixels) const;
    std::vector<std::uint8_t> ToBitmap(std::span<const std::uint8_t> pixels) const;
private:
    std::vector<std::uint8_t> original_, pixels_;
};
}
