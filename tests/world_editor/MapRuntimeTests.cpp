#include "WorldMapCatalog.h"
#include "WorldObjectCandidate.h"
#include "WorldObjectCodec.h"
#include "TerrainAttributeCodec.h"
#include "TerrainHeightFile.h"
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace Silver::WorldEditor;

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Data directory required");
        const fs::path data = argv[1];
        const auto maps = WorldMapCatalog::Discover(data);
        if (maps.empty()) throw std::runtime_error("No editable worlds discovered");

        std::set<int> worlds;
        int failed = 0;
        for (const auto& map : maps)
        {
            try
            {
                worlds.insert(map.WorldNumber);

                const auto directory = data / map.Folder;
                const auto objectPath = directory / map.ObjectFile;
                const auto objectBytes = WorldObjectCandidate::Read(objectPath);
                const auto object = WorldObjectCodec::Decode(objectBytes);
                if (object.Map != map.WorldNumber)
                    throw std::runtime_error("OBJ header does not match physical WorldN");
                if (WorldObjectCodec::Encode(object) != objectBytes)
                    throw std::runtime_error("OBJ does not round-trip byte-for-byte");

                std::size_t opaque = 0;
                for (const auto& record : object.Records)
                {
                    if (record.BaselineOnly) ++opaque;
                    else (void)LoadedSpatialBlock(record);
                }

                const auto attributePath = directory / ("EncTerrain" + std::to_string(map.WorldNumber) + ".att");
                const auto attributes = TerrainAttributeCodec::Decode(WorldObjectCandidate::Read(attributePath));
                if (attributes.Map != map.WorldNumber)
                    throw std::runtime_error("ATT header does not match physical WorldN");

                const auto heightBytes = WorldObjectCandidate::Read(directory / "TerrainHeight.OZB");
                const bool extended = WorldMapCatalog::UsesExtendedHeight(map.MapId);
                const bool terrainEditable = !extended && TerrainHeightFile::SupportsLegacyEditing(heightBytes);
                if (terrainEditable)
                {
                    const auto height = TerrainHeightFile::DecodeOzb(heightBytes);
                    if (height.Pixels().size() != TerrainCells || height.Encode(height.Pixels()) != heightBytes)
                        throw std::runtime_error("Legacy terrain height does not round-trip");
                }
                else if (!extended || heightBytes.size() != 4 + 14 + 40 + TerrainCells * 3)
                {
                    throw std::runtime_error("Unknown non-legacy terrain height layout");
                }

                std::cout << "MAP_OK map=" << map.MapId
                          << " world=" << map.WorldNumber
                          << " objects=" << object.Records.size()
                          << " opaque=" << opaque
                          << " terrain=" << (terrainEditable ? "legacy-editable" : "24bit-objects-only")
                          << " folder=" << map.Folder << '\n';
            }
            catch (const std::exception& error)
            {
                ++failed;
                std::cerr << "MAP_FAIL map=" << map.MapId
                          << " world=" << map.WorldNumber
                          << " folder=" << map.Folder
                          << " reason=" << error.what() << '\n';
            }
        }

        std::cout << "MAP_RUNTIME_CHECKS maps=" << maps.size()
                  << " unique_worlds=" << worlds.size()
                  << " failed=" << failed << "\n";
        return failed == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MAP_RUNTIME_FAILURE " << error.what() << '\n';
        return 1;
    }
}
