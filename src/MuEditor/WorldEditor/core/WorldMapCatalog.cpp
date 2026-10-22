#include "WorldMapCatalog.h"
#include <algorithm>
#include <stdexcept>

namespace Silver::WorldEditor
{
int WorldMapCatalog::PhysicalWorldNumber(int mapId)
{
    if (mapId < 0 || mapId > 96) return -1;
    if (mapId == 32) return 10;
    if ((mapId >= 11 && mapId <= 17) || mapId == 52) return 12;
    if ((mapId >= 18 && mapId <= 23) || mapId == 53) return 19;
    if ((mapId >= 24 && mapId <= 29) || mapId == 36) return 25;
    if (mapId >= 45 && mapId <= 50) return 47;
    return mapId + 1;
}

bool WorldMapCatalog::UsesExtendedHeight(int mapId)
{
    // Mirrors MuMain IsTerrainHeightExtMap for the physical editor maps:
    // 42 = 3rd class-change stage 2, 63 = PK Field, 66 = Doppelganger 2.
    return mapId == 42 || mapId == 63 || mapId == 66;
}

bool WorldMapCatalog::SceneOnly(int mapId)
{
    switch (mapId)
    {
    case 32: // MuMain normalizes this alias back to Map9 during LoadWorld.
    case 54:
    case 55:
    case 73:
    case 74:
    case 77:
    case 78:
        return true;
    default:
        return false;
    }
}

std::vector<WorldMapEntry> WorldMapCatalog::Discover(const std::filesystem::path& dataRoot)
{
    if (!std::filesystem::is_directory(dataRoot))
        throw std::invalid_argument("No se encontro la carpeta Data del editor.");

    std::vector<WorldMapEntry> result;
    for (int mapId = 0; mapId <= 96; ++mapId)
    {
        if (SceneOnly(mapId)) continue;
        const int world = PhysicalWorldNumber(mapId);
        if (world <= 0) continue;

        const std::string folder = "World" + std::to_string(world);
        const auto directory = dataRoot / folder;
        const std::string stem = "EncTerrain" + std::to_string(world);
        const auto object = directory / (stem + ".obj");
        const auto mapping = directory / (stem + ".map");
        const auto attributes = directory / (stem + ".att");
        const auto height = directory / "TerrainHeight.OZB";
        if (!std::filesystem::is_directory(directory)
            || !std::filesystem::is_regular_file(object)
            || !std::filesystem::is_regular_file(mapping)
            || !std::filesystem::is_regular_file(attributes)
            || !std::filesystem::is_regular_file(height))
            continue;

        result.push_back({mapId, world, folder, stem + ".obj"});
    }

    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
    {
        return left.MapId < right.MapId;
    });
    return result;
}
}
