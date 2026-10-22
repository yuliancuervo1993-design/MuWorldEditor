#include "WorldGlobalAssetRegistry.h"
#include "WorldFileIo.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Silver::WorldEditor;

namespace
{
void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
    std::cout << "PASS " << message << "\n";
}

void Write(const std::filesystem::path& path, std::size_t bytes = 16)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    for (std::size_t index = 0; index < bytes; ++index)
        output.put(static_cast<char>((index * 17) & 0xFF));
}
}

int main(int argc, char** argv)
{
    const auto root = std::filesystem::current_path() / ".tools" / "global-registry-tests-runtime";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    try
    {
        const auto data = root / "Data";
        Write(data / "Object1" / "Tree01.bmd");
        Write(data / "Item" / "Sword01.bmd");
        Write(data / "Player" / "Helm01.bmd");
        Write(data / "Object1" / "Object01.bmd");
        Write(data / "Object34" / Utf8Path("나무01.bmd"));

        Write(data / "World1" / "TileGrass01.OZJ");
        Write(data / "World1" / "TileRock01.OZJ");
        Write(data / "World1" / "ExtTile01.OZJ");
        Write(data / "World2" / "TileGrass01.OZT");
        Write(data / "World2" / "TileGround01.OZJ");
        Write(data / "World2" / "TileGrass01_R.OZJ"); // map-grass overlay, not paintable MAP slot

        WorldGlobalAssetRegistry registry;
        registry.Scan(data, root / "ImportedModels");

        Check(registry.Models().size() == 5, "registry indexes global BMD files");
        Check(registry.ModelFolderCount() == 4, "registry counts ObjectN folders");
        Check(std::any_of(registry.Models().begin(), registry.Models().end(),
            [](const auto& asset) { return asset.Name == "나무01"; }), "registry preserves UTF-8 model names");
        Check(registry.TerrainWorldCount() == 2, "registry counts WorldN terrain roots");
        Check(registry.TerrainMaterials().size() == 5, "registry preserves same-name terrain variants across worlds");

        const auto grass = std::find_if(registry.TerrainMaterials().begin(), registry.TerrainMaterials().end(),
            [](const auto& asset) { return asset.Key == "tilegrass01"; });
        Check(grass != registry.TerrainMaterials().end(), "grass material indexed");
        Check(grass->Slot == 0, "grass slot matches MapManager");
        Check(grass->Worlds.size() == 1, "a terrain variant belongs to its exact source world");
        Check(grass->Occurrences == 1, "terrain variants do not alias different textures");

        Check(WorldGlobalAssetRegistry::TerrainSlot("TileGround01.jpg") == 2, "ground slot matches MapManager");
        Check(WorldGlobalAssetRegistry::TerrainSlot("TileWater01.OZJ") == 5, "water slot matches MapManager");
        Check(WorldGlobalAssetRegistry::TerrainSlot("TileWood01.jpg") == 6, "wood slot matches MapManager");
        Check(WorldGlobalAssetRegistry::TerrainSlot("TileRock07.jpg") == 13, "rock07 slot matches MapManager");
        Check(WorldGlobalAssetRegistry::TerrainSlot("ExtTile01.jpg") == 14, "ExtTile01 slot matches MapManager");
        Check(WorldGlobalAssetRegistry::TerrainSlot("ExtTile16.jpg") == 29, "ExtTile16 slot matches MapManager");
        Check(!WorldGlobalAssetRegistry::TerrainSlot("TileGrass01_R.jpg"), "map-grass overlay is not exposed as paintable slot");

        Check(WorldGlobalAssetRegistry::TerrainCategory("TileGrass01.OZJ") == "Cesped", "grass category");
        Check(WorldGlobalAssetRegistry::TerrainCategory("TileRock03.OZJ") == "Roca", "rock category");
        Check(WorldGlobalAssetRegistry::TerrainCategory("ExtTile08.OZJ") == "Extendido", "extended category");

        if (argc > 1)
        {
            WorldGlobalAssetRegistry runtime;
            runtime.Scan(std::filesystem::path(argv[1]), root / "runtime-imported-none");
            Check(runtime.Models().size() >= 3000, "runtime registry sees thousands of global BMD files");
            Check(runtime.ModelFolderCount() >= 40, "runtime registry sees many ObjectN folders");
            Check(runtime.TerrainMaterials().size() >= 10, "runtime registry sees paintable terrain materials");
            Check(runtime.TerrainWorldCount() >= 40, "runtime registry sees many WorldN roots");
            std::cout << "RUNTIME REGISTRY "
                      << runtime.Models().size() << " BMD | "
                      << runtime.ModelFolderCount() << " ObjectN | "
                      << runtime.TerrainMaterials().size() << " suelos | "
                      << runtime.TerrainWorldCount() << " WorldN\n";
        }

        std::filesystem::remove_all(root, ignored);
        std::cout << "ALL GLOBAL ASSET REGISTRY TESTS PASSED\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << "\n";
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
}
