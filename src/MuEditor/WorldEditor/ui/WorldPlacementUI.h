#pragma once
#include "IWorldEditorScene.h"
#include "../core/WorldPlacementSession.h"
#include <string>
namespace Silver::WorldEditor
{
class WorldPlacementUI
{
public:
    void Choose(const WorldObjectRecord& prototype, const WorldObjectDocument& document, IWorldEditorScene& scene);
    void Cancel() { placement_.Cancel(); }
    bool Active() const { return placement_.Active(); }
    void Controls();
    // Returns true when the placement tool owns this frame's mouse event.
    bool Update(WorldObjectDocument& document, IWorldEditorScene& scene, ObjectId& selected);
private:
    void Shortcuts();
    void DrawTarget(IWorldEditorScene& scene) const;
    const char* StatusText() const;
    WorldPlacementSession placement_;
    bool usePivot_ = false;
    std::string error_;
};
}
