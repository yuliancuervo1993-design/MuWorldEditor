#pragma once
#include "WorldObjectRecord.h"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
struct WorldPrefab
{
    std::string Name;
    std::vector<WorldObjectRecord> Records;
};

class WorldPrefabCodec
{
public:
    static constexpr const char* Schema = "silver-world-editor-prefab-v1";
    static constexpr std::size_t MaximumRecords = 256;

    static WorldPrefab FromSelection(
        std::string name,
        std::span<const WorldObjectRecord> records,
        const std::array<float, 3>& pivot);
    static std::vector<WorldObjectRecord> Instantiate(
        const WorldPrefab& prefab,
        const std::array<float, 3>& position);
    static std::vector<std::uint8_t> Encode(const WorldPrefab& prefab);
    static WorldPrefab Decode(std::span<const std::uint8_t> bytes);
};
}
