#include "TerrainAttributeCodec.h"
#include "WorldObjectCodec.h"
#include <array>
#include <stdexcept>
namespace Silver::WorldEditor
{
namespace
{
constexpr std::array<std::uint8_t, 3> BuxKey{0xFC, 0xCF, 0xAB};
void Bux(std::vector<std::uint8_t>& bytes)
{
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] ^= BuxKey[i % BuxKey.size()];
}
}
TerrainAttributeFile TerrainAttributeCodec::Decode(std::span<const std::uint8_t> input)
{
    const bool wide = input.size() == TerrainCells * 2 + 3 || input.size() == TerrainCells * 2 + 4;
    const bool encrypted = input.size() == TerrainCells + 4 || input.size() == TerrainCells * 2 + 4;
    const std::size_t header = encrypted ? 4 : 3;
    if (input.size() != header + TerrainCells * (wide ? 2 : 1)) throw std::invalid_argument("Tamano ATT no compatible.");
    auto bytes = encrypted ? WorldObjectCodec::Decrypt(input) : std::vector<std::uint8_t>(input.begin(), input.end());
    if (encrypted) Bux(bytes);
    if (bytes[0] != 0 || bytes[header - 2] != 255 || bytes[header - 1] != 255)
        throw std::invalid_argument("Cabecera ATT no compatible.");
    TerrainAttributeFile file;
    file.Encoding = encrypted ? (wide ? AttributeEncoding::Encrypted16 : AttributeEncoding::Encrypted8)
        : (wide ? AttributeEncoding::Raw16 : AttributeEncoding::Raw8);
    file.Map = encrypted ? bytes[1] : 0;
    file.Cells.resize(TerrainCells);
    for (std::size_t i = 0; i < file.Cells.size(); ++i)
    {
        const auto offset = header + i * (wide ? 2 : 1);
        file.Cells[i] = bytes[offset] | (wide ? static_cast<std::uint16_t>(bytes[offset + 1] << 8) : 0);
    }
    return file;
}
std::vector<std::uint8_t> TerrainAttributeCodec::Encode(const TerrainAttributeFile& file)
{
    if (file.Encoding < AttributeEncoding::Raw8 || file.Encoding > AttributeEncoding::Encrypted16) throw std::invalid_argument("Formato ATT desconocido.");
    if (file.Cells.size() != TerrainCells) throw std::invalid_argument("Cantidad ATT incorrecta.");
    const bool encrypted = file.Encoding == AttributeEncoding::Encrypted8 || file.Encoding == AttributeEncoding::Encrypted16;
    const bool wide = file.Encoding == AttributeEncoding::Raw16 || file.Encoding == AttributeEncoding::Encrypted16;
    std::vector<std::uint8_t> bytes{0};
    bytes.reserve((encrypted ? 4 : 3) + TerrainCells * (wide ? 2 : 1));
    if (encrypted) bytes.push_back(file.Map);
    bytes.push_back(255); bytes.push_back(255);
    for (auto value : file.Cells)
    {
        if (!wide && value > 255) throw std::invalid_argument("ATT de 8 bits no admite flags superiores.");
        bytes.push_back(static_cast<std::uint8_t>(value));
        if (wide) bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }
    if (!encrypted) return bytes;
    Bux(bytes);
    return WorldObjectCodec::Encrypt(bytes);
}
}
