#pragma once
#include "WorldObjectRecord.h"
#include <span>
#include <vector>

namespace Silver::WorldEditor
{
enum class ObjectEncoding { LegacyPlain, PlainWithMap, MapEncrypted };
struct WorldObjectFile
{
    ObjectEncoding Encoding = ObjectEncoding::MapEncrypted;
    std::uint8_t Version = 0;
    std::uint8_t Map = 0;
    std::vector<WorldObjectRecord> Records;
};

class WorldObjectCodec
{
public:
    static WorldObjectFile Decode(std::span<const std::uint8_t> bytes);
    static std::vector<std::uint8_t> Encode(const WorldObjectFile& file);
    static std::vector<std::uint8_t> Encrypt(std::span<const std::uint8_t> bytes);
    static std::vector<std::uint8_t> Decrypt(std::span<const std::uint8_t> bytes);
};
}
