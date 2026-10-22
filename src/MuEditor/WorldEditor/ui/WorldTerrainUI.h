#pragma once
#include "IWorldEditorScene.h"
#include "../core/WorldTerrainDocument.h"
#include "../core/WorldGlobalAssetRegistry.h"
#include <optional>
namespace Silver::WorldEditor
{
class WorldTerrainUI
{
public:
    bool Draw(WorldTerrainDocument& document, IWorldEditorScene& scene, bool* open = nullptr, bool resetLayout = false);
    void Cancel(WorldTerrainDocument& document);
private:
    void Controls(WorldTerrainDocument& document, IWorldEditorScene& scene, bool& save);
    void MaterialLibrary(WorldTerrainDocument& document, IWorldEditorScene& scene);
    void SizeControls();
    void Input(WorldTerrainDocument& document, IWorldEditorScene& scene);
    void Overlay(const WorldTerrainDocument& document, IWorldEditorScene& scene);
    TerrainBrush brush_;
    bool showZones_ = true, enabled_ = true;
    bool showMaterialPreview_ = true;
    bool showZonesDuringMaterial_ = false;
    float materialPreviewOpacity_ = 0.58f;
    bool hasHit_ = false;
    std::array<float, 3> hit_{};
    std::optional<std::array<int, 2>> rampAnchor_;
    WorldGlobalAssetRegistry registry_;
    bool registryAttempted_ = false;
    int materialCategory_ = 0;
    char materialSearch_[64]{};
    std::string registryError_;
    std::string message_;
};
}
