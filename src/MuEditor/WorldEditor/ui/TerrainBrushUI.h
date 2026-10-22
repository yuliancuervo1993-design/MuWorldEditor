#pragma once
#include "IWorldEditorScene.h"
#include "../core/TerrainBrushDocument.h"
#include <string>
namespace Silver::WorldEditor
{
class TerrainBrushUI
{
public:
    bool Draw(TerrainBrushDocument& document, IWorldEditorScene& scene);
    void SetStatus(std::string message) { message_=std::move(message); }
private:
    void DrawControls(TerrainBrushDocument& document, bool& save);
    void PaintViewport(TerrainBrushDocument& document, IWorldEditorScene& scene);
    void DrawOverlay(const TerrainBrushDocument& document, IWorldEditorScene& scene, int x, int y);
    int tool_=0, radius_=2, strength_=4, level_=32;
    bool erase_=false, showZones_=true, sampleLevel_=true, waitRelease_=false;
    std::string message_;
};
}
