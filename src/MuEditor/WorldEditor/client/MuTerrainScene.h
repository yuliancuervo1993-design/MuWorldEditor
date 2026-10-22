#pragma once
#ifdef _EDITOR
#include "../core/WorldTerrainDocument.h"
namespace Silver::WorldEditor
{
class MuTerrainScene
{
public:
    void Attach(const WorldTerrainDocument& document);
    void Invalidate() { attached_ = false; worldId_ = -1; }
    void Synchronize(const WorldTerrainDocument& document);
private:
    void RebuildNormal(int index);
    int worldId_ = -1;
    bool attached_ = false;
    std::uint64_t revision_ = 0;
    std::vector<std::uint8_t> heights_, normalsDirty_;
    std::vector<std::uint16_t> attributes_;
    std::optional<TerrainMappingFile> mapping_;
};
}
#endif
