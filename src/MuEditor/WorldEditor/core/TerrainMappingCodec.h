#pragma once
#include "TerrainAttributeCodec.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace Silver::WorldEditor
{
inline constexpr std::size_t TerrainMappingHeaderSize = 2;
inline constexpr std::size_t TerrainMappingLayerCount = 3;
inline constexpr std::size_t TerrainMappingSerializedSize =
    TerrainMappingHeaderSize + TerrainCells * TerrainMappingLayerCount;

// The runtime loader consumes encrypted EncTerrainN.map files. Terrain.map is
// a separate legacy export with a different size and representation.
struct TerrainMappingFile
{
    TerrainMappingFile()
        : Layer1(TerrainCells), Layer2(TerrainCells), Alpha(TerrainCells)
    {
    }

    std::uint8_t Version = 0;
    std::uint8_t Map = 0;
    std::vector<std::uint8_t> Layer1;
    std::vector<std::uint8_t> Layer2;
    std::vector<std::uint8_t> Alpha;
    bool operator==(const TerrainMappingFile&) const = default;
};
class TerrainMappingCodec
{
public:
    static TerrainMappingFile Decode(std::span<const std::uint8_t> encrypted);
    static std::vector<std::uint8_t> Encode(const TerrainMappingFile& file);
};
}
