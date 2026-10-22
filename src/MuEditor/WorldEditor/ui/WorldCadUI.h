#pragma once
#include "IWorldEditorScene.h"
#include "../core/WorldCadLayout.h"
#include <filesystem>
#include <string>

namespace Silver::WorldEditor
{
class WorldCadUI
{
public:
    void Draw(WorldObjectDocument& document, IWorldEditorScene& scene, ObjectId selected, bool& open);
    void Reset();
private:
    bool DrawParameters(IWorldEditorScene& scene);
    void DrawSaved(int mapId);
    void ClearPreview(IWorldEditorScene& scene);
    WorldCadLayout layout_;
    std::vector<WorldObjectRecord> preview_;
    std::vector<std::filesystem::path> saved_;
    std::uint64_t previewRevision_ = 0;
    std::string status_;
    bool initialized_ = false;
};
}
