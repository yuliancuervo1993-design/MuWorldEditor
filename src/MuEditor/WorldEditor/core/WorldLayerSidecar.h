#pragma once
#include "WorldLayerDocument.h"
#include "WorldObjectCodec.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
struct WorldLayerSidecarExport
{
    std::vector<std::uint8_t> Bytes;
    std::size_t PersistentAssignments = 0;
    std::size_t AmbiguousAssignmentsSkipped = 0;
};

struct WorldLayerSidecarRestore
{
    WorldLayerDocument Layers;
    std::size_t RestoredAssignments = 0;
    std::size_t AmbiguousAssignmentsSkipped = 0;
    std::size_t MissingAssignmentsSkipped = 0;
};

class WorldLayerSidecar
{
public:
    static constexpr const char* Schema = "silver-world-editor-layers-v1";
    static constexpr const char* FileName = "SilverWorldEditor.layers.json";

    static std::string ObjectKey(const WorldObjectRecord& record);
    static WorldLayerSidecarExport Encode(
        const WorldObjectFile& baseline,
        const WorldLayerDocument& layers,
        int mapId,
        int worldNumber);
    static WorldLayerSidecarRestore Decode(
        std::span<const std::uint8_t> bytes,
        const WorldObjectFile& baseline,
        int mapId,
        int worldNumber);
};
}
