#include "TerrainMappingCodec.h"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace Silver::WorldEditor
{
namespace
{
constexpr std::uint8_t InitialMapKey = 0x5E;
constexpr std::uint8_t MapKeyIncrement = 0x3D;
constexpr std::array<std::uint8_t, 16> MapXorKey{
    0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
    0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2};

std::vector<std::uint8_t> Crypt(std::span<const std::uint8_t> input, bool encrypt)
{
    std::vector<std::uint8_t> output(input.size());
    std::uint8_t mapKey = InitialMapKey;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (encrypt)
        {
            output[i] = static_cast<std::uint8_t>((input[i] + mapKey) ^ MapXorKey[i % MapXorKey.size()]);
            mapKey = static_cast<std::uint8_t>(output[i] + MapKeyIncrement);
            continue;
        }

        output[i] = static_cast<std::uint8_t>((input[i] ^ MapXorKey[i % MapXorKey.size()]) - mapKey);
        mapKey = static_cast<std::uint8_t>(input[i] + MapKeyIncrement);
    }
    return output;
}

void AppendLayer(std::vector<std::uint8_t>& output, std::span<const std::uint8_t> layer)
{
    output.insert(output.end(), layer.begin(), layer.end());
}
TerrainMappingFile DecodeValidated(std::span<const std::uint8_t> encrypted)
{
    const auto plain = Crypt(encrypted, false);
    TerrainMappingFile file;
    file.Version = plain[0];
    file.Map = plain[1];
    auto source = plain.begin() + TerrainMappingHeaderSize;
    std::copy_n(source, file.Layer1.size(), file.Layer1.begin());
    source += file.Layer1.size();
    std::copy_n(source, file.Layer2.size(), file.Layer2.begin());
    source += file.Layer2.size();
    std::copy_n(source, file.Alpha.size(), file.Alpha.begin());
    return file;
}
}
TerrainMappingFile TerrainMappingCodec::Decode(std::span<const std::uint8_t> encrypted)
{
    // Reject malformed/legacy layouts before entering the heavy decode frame.
    // TerrainMappingFile is almost 200 KiB; allocating it for an input that
    // will be rejected can exhaust the Windows debug stack when another map
    // is already alive in the caller.
    if (encrypted.size() != TerrainMappingSerializedSize)
        throw std::invalid_argument("EncTerrain.map tiene un tamano inesperado.");

    return DecodeValidated(encrypted);
}
std::vector<std::uint8_t> TerrainMappingCodec::Encode(const TerrainMappingFile& file)
{
    if (file.Layer1.size() != TerrainCells || file.Layer2.size() != TerrainCells || file.Alpha.size() != TerrainCells)
        throw std::invalid_argument("EncTerrain.map tiene capas con una cantidad de celdas invalida.");

    std::vector<std::uint8_t> plain;
    plain.reserve(TerrainMappingSerializedSize);
    plain.push_back(file.Version);
    plain.push_back(file.Map);
    AppendLayer(plain, file.Layer1);
    AppendLayer(plain, file.Layer2);
    AppendLayer(plain, file.Alpha);
    return Crypt(plain, true);
}
}
