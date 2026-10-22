#include "stdafx.h"
#ifdef _EDITOR
#include "MuWorldScene.h"
#include "Render/Terrain/ZzzLodTerrain.h"
#include "World/MapInfra/MapManager.h"
#include <cmath>
namespace Silver::WorldEditor
{
bool MuWorldScene::ModelReady(std::int16_t type) const
{
    auto* mesh = library_.Mesh(type);
    return attached_ && gMapManager.WorldActive == worldId_ && (mesh ? mesh : &placementMesh_)->Prepare(type);
}
bool MuWorldScene::HeightAt(float x, float y, float& height) const
{
    if (!attached_ || gMapManager.WorldActive != worldId_ || !std::isfinite(x) || !std::isfinite(y)
        || x < 0 || y < 0 || x >= MapEdge || y >= MapEdge) return false;
    height = RequestTerrainHeight(x, y); return std::isfinite(height);
}
bool MuWorldScene::MinimumModelZ(const WorldObjectRecord& record, float& minimum) const
{
    auto* mesh = library_.Mesh(record.Type);
    return ModelReady(record.Type) && (mesh ? mesh : &placementMesh_)->MinimumZ(record, minimum);
}
bool MuWorldScene::DrawPlacementPreview()
{
    if (!attached_ || gMapManager.WorldActive != worldId_) return true;
    try
    {
        const auto draw = [this](const WorldObjectRecord& record) {
            auto* mesh = library_.Mesh(record.Type); return (mesh ? mesh : &placementMesh_)->Draw(record);
        };
        if (previewRecord_) return draw(*previewRecord_);
        for (const auto& record : previewRecords_) if (!draw(record)) return false;
        return true;
    }
    catch (const std::exception&) { previewRecord_.reset(); previewRecords_.clear(); return false; }
}
}
#endif
