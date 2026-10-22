#include "WorldObjectCodec.h"
#include <bit>
#include <limits>
#include <stdexcept>

namespace Silver::WorldEditor
{
namespace
{
constexpr std::size_t RecordBytes = 30;
constexpr std::size_t LegacyHeaderBytes = 3;
constexpr std::size_t MapHeaderBytes = 4;
constexpr std::array<std::uint8_t, 16> MapKey{0xD1,0x73,0x52,0xF6,0xD2,0x9A,0xCB,0x27,0x3E,0xAF,0x59,0x31,0x37,0xB3,0xE7,0xA2};
constexpr unsigned InitialKey = 0x5E;
constexpr unsigned KeyIncrement = 0x3D;
constexpr std::uint8_t EncryptedVersionZero = 0x8F;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

std::uint32_t ReadLittle(std::span<const std::uint8_t> bytes, std::size_t& cursor, std::size_t count)
{
    if (cursor > bytes.size() || count > bytes.size() - cursor) throw std::invalid_argument("Archivo OBJ truncado.");
    std::uint32_t value = 0;
    for (std::size_t shift = 0; shift < count; ++shift)
        value |= static_cast<std::uint32_t>(bytes[cursor++]) << (shift * 8);
    return value;
}
void AppendLittle(std::vector<std::uint8_t>& bytes, std::uint32_t value, std::size_t count)
{
    for (std::size_t shift = 0; shift < count; ++shift)
        bytes.push_back(static_cast<std::uint8_t>(value >> (shift * 8)));
}
WorldObjectRecord ReadRecord(std::span<const std::uint8_t> bytes, std::size_t& cursor)
{
    WorldObjectRecord result;
    result.Type = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(ReadLittle(bytes, cursor, 2)));
    for (float& value : result.Position) value = std::bit_cast<float>(ReadLittle(bytes, cursor, 4));
    for (float& value : result.Angle) value = std::bit_cast<float>(ReadLittle(bytes, cursor, 4));
    result.Scale = std::bit_cast<float>(ReadLittle(bytes, cursor, 4));
    try
    {
        LoadedSpatialBlock(result);
    }
    catch (const std::invalid_argument&)
    {
        result.BaselineOnly = true;
    }
    return result;
}
void AppendRecord(std::vector<std::uint8_t>& bytes, const WorldObjectRecord& record)
{
    if (!record.BaselineOnly) ValidateRecord(record);
    AppendLittle(bytes, static_cast<std::uint16_t>(record.Type), 2);
    for (float value : record.Position) AppendLittle(bytes, std::bit_cast<std::uint32_t>(value), 4);
    for (float value : record.Angle) AppendLittle(bytes, std::bit_cast<std::uint32_t>(value), 4);
    AppendLittle(bytes, std::bit_cast<std::uint32_t>(record.Scale), 4);
}
}

std::vector<std::uint8_t> WorldObjectCodec::Encrypt(std::span<const std::uint8_t> bytes)
{
    std::vector<std::uint8_t> result(bytes.size());
    unsigned key = InitialKey;
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        result[index] = static_cast<std::uint8_t>((bytes[index] + key) ^ MapKey[index % MapKey.size()]);
        key = (result[index] + KeyIncrement) & 0xFF;
    }
    return result;
}
std::vector<std::uint8_t> WorldObjectCodec::Decrypt(std::span<const std::uint8_t> bytes)
{
    std::vector<std::uint8_t> result(bytes.size());
    unsigned key = InitialKey;
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        result[index] = static_cast<std::uint8_t>((bytes[index] ^ MapKey[index % MapKey.size()]) - key);
        key = (bytes[index] + KeyIncrement) & 0xFF;
    }
    return result;
}

WorldObjectFile WorldObjectCodec::Decode(std::span<const std::uint8_t> input)
{
    if (input.size() < LegacyHeaderBytes || input.size() > MaximumObjects * RecordBytes + MapHeaderBytes)
        throw std::invalid_argument("Tamano OBJ fuera de limites.");
    WorldObjectFile file;
    auto decrypted = input[0] == EncryptedVersionZero ? Decrypt(input) : std::vector<std::uint8_t>{};
    const auto bytes = decrypted.empty() ? input : std::span<const std::uint8_t>(decrypted);
    const bool legacy = decrypted.empty() && (input.size() - LegacyHeaderBytes) % RecordBytes == 0;
    file.Encoding = !decrypted.empty() ? ObjectEncoding::MapEncrypted : (legacy ? ObjectEncoding::LegacyPlain : ObjectEncoding::PlainWithMap);
    std::size_t cursor = 0;
    file.Version = static_cast<std::uint8_t>(ReadLittle(bytes, cursor, 1));
    if (file.Version != 0) throw std::invalid_argument("Version OBJ no compatible; no se modifico el original.");
    if (!legacy) file.Map = static_cast<std::uint8_t>(ReadLittle(bytes, cursor, 1));
    const auto count = ReadLittle(bytes, cursor, 2);
    if (count > MaximumObjects || bytes.size() - cursor != count * RecordBytes)
        throw std::invalid_argument("Cantidad de objetos o longitud OBJ inconsistente.");
    file.Records.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) file.Records.push_back(ReadRecord(bytes, cursor));
    return file;
}

std::vector<std::uint8_t> WorldObjectCodec::Encode(const WorldObjectFile& file)
{
    if (file.Version != 0 || file.Records.size() > MaximumObjects) throw std::invalid_argument("Version o cantidad OBJ no valida.");
    if (file.Encoding != ObjectEncoding::LegacyPlain && file.Encoding != ObjectEncoding::PlainWithMap
        && file.Encoding != ObjectEncoding::MapEncrypted) throw std::invalid_argument("Formato OBJ no reconocido.");
    const bool legacy = file.Encoding == ObjectEncoding::LegacyPlain;
    std::vector<std::uint8_t> bytes;
    bytes.reserve((legacy ? LegacyHeaderBytes : MapHeaderBytes) + file.Records.size() * RecordBytes);
    bytes.push_back(file.Version);
    if (!legacy) bytes.push_back(file.Map);
    AppendLittle(bytes, static_cast<std::uint16_t>(file.Records.size()), 2);
    for (const auto& record : file.Records) AppendRecord(bytes, record);
    return file.Encoding == ObjectEncoding::MapEncrypted ? Encrypt(bytes) : bytes;
}
}
