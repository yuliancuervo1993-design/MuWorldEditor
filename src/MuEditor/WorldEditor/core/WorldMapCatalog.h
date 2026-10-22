#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
struct WorldMapEntry
{
    int MapId = -1;
    int WorldNumber = -1;
    std::string Folder;
    std::string ObjectFile;
};

class WorldMapCatalog
{
public:
    static int PhysicalWorldNumber(int mapId);
    static bool SceneOnly(int mapId);
    static bool UsesExtendedHeight(int mapId);
    static std::vector<WorldMapEntry> Discover(const std::filesystem::path& dataRoot);
};
}
