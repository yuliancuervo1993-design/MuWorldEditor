#pragma once
#include "WorldModelCatalog.h"
#include "WorldModelPackage.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
struct WorldTerrainMaterialAsset
{
    int Slot = -1;
    std::string Key;
    std::string Identity; // Distinguish the same slot name in different worlds.
    std::string Name;
    std::string Category;
    std::vector<int> Worlds;
    std::vector<std::string> RelativePaths;
    std::size_t Occurrences = 0;
};

class WorldGlobalAssetRegistry
{
public:
    void Scan(const std::filesystem::path& dataRoot = "Data",
        const std::filesystem::path& importedRoot = WorldModelPackage::ImportedRoot());

    const std::vector<WorldModelAsset>& Models() const { return models_.Assets(); }
    const std::vector<WorldTerrainMaterialAsset>& TerrainMaterials() const { return terrain_; }
    const std::vector<WorldModelPackageEntry>& ImportedModels() const { return imported_; }

    std::size_t ModelFolderCount() const { return models_.FolderCount(); }
    std::size_t TerrainWorldCount() const { return terrainWorlds_; }

    static std::optional<int> TerrainSlot(std::string nameOrPath);
    static std::string TerrainKey(std::string nameOrPath);
    static std::string TerrainCategory(std::string nameOrPath);
    static std::string TerrainDisplayName(std::string nameOrPath);

private:
    WorldModelCatalog models_;
    std::vector<WorldTerrainMaterialAsset> terrain_;
    std::vector<WorldModelPackageEntry> imported_;
    std::size_t terrainWorlds_ = 0;
};
}
