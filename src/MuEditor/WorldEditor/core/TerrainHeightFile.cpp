#include "TerrainHeightFile.h"
#include <algorithm>
#include <stdexcept>
namespace Silver::WorldEditor
{
namespace
{
constexpr std::size_t OzbPrefix = 4;
constexpr std::size_t BitmapHeader = 14 + 40 + 256 * 4;
constexpr std::size_t PixelsOffset = OzbPrefix + BitmapHeader;
constexpr std::size_t LegacyRequired = PixelsOffset + TerrainCells;
constexpr std::size_t ExtendedRequired = OzbPrefix + 14 + 40 + TerrainCells * 3;
void Store32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
}
bool TerrainHeightFile::SupportsLegacyEditing(std::span<const std::uint8_t> bytes)
{
    // The legacy loader ignores BMP metadata and only requires at least 66618
    // bytes. Some shipped files append two bytes and one even advertises 24 bpp
    // despite being read by OpenTerrainHeight. The true extended maps are
    // selected by map ID in WorldMapCatalog and use the 196666-byte layout.
    return bytes.size() >= LegacyRequired && bytes.size() < ExtendedRequired;
}
TerrainHeightFile TerrainHeightFile::DecodeOzb(std::span<const std::uint8_t> bytes)
{
    // MuMain's OpenTerrainHeight requires at least 66618 bytes: four wrapper
    // bytes, 1078 bytes of BMP/header data and then 65536 8-bit samples.
    // Several shipped worlds append two bytes; MuMain ignores them, so preserve
    // them byte-for-byte when encoding instead of rejecting the file.
    if (!SupportsLegacyEditing(bytes))
        throw std::invalid_argument("OZB no compatible con el pincel legacy de 8 bits.");
    TerrainHeightFile result;
    result.original_.assign(bytes.begin(), bytes.end());
    result.pixels_.assign(bytes.begin() + PixelsOffset, bytes.begin() + LegacyRequired);
    return result;
}
std::vector<std::uint8_t> TerrainHeightFile::Encode(std::span<const std::uint8_t> pixels) const
{
    if (original_.size() < LegacyRequired || pixels.size() != TerrainCells)
        throw std::invalid_argument("Alturas o plantilla no inicializadas.");
    auto result = original_;
    std::copy(pixels.begin(), pixels.end(), result.begin() + PixelsOffset);
    return result;
}
std::vector<std::uint8_t> TerrainHeightFile::ToBitmap(std::span<const std::uint8_t> pixels) const
{
    if (pixels.size() != TerrainCells) throw std::invalid_argument("Cantidad de alturas incorrecta.");
    std::vector<std::uint8_t> bytes(BitmapHeader + TerrainCells, 0);
    bytes[0] = 'B'; bytes[1] = 'M';
    Store32(bytes, 2, static_cast<std::uint32_t>(bytes.size())); Store32(bytes, 10, BitmapHeader);
    Store32(bytes, 14, 40); Store32(bytes, 18, TerrainSide); Store32(bytes, 22, TerrainSide);
    bytes[26] = 1; bytes[28] = 8; Store32(bytes, 34, TerrainCells); Store32(bytes, 46, 256);
    for (int i = 0; i < 256; ++i)
        for (int channel = 0; channel < 3; ++channel) bytes[54 + i * 4 + channel] = static_cast<std::uint8_t>(i);
    std::copy(pixels.begin(), pixels.end(), bytes.begin() + BitmapHeader);
    return bytes;
}
}
