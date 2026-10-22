#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace Silver::WorldEditor
{
inline constexpr int TerrainSide = 256;
inline constexpr int TerrainCells = TerrainSide * TerrainSide;
enum class AttributeEncoding { Raw8, Raw16, Encrypted8, Encrypted16 };
struct TerrainAttributeFile
{
    AttributeEncoding Encoding = AttributeEncoding::Encrypted16;
    std::uint8_t Map = 0;
    std::vector<std::uint16_t> Cells;
};
class TerrainAttributeCodec
{
public:
    static TerrainAttributeFile Decode(std::span<const std::uint8_t> bytes);
    static std::vector<std::uint8_t> Encode(const TerrainAttributeFile& file);
};
}
