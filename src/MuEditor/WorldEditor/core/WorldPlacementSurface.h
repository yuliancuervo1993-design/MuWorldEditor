#pragma once
#include "WorldObjectRecord.h"

namespace Silver::WorldEditor
{
// Read-only adapter. Implementations must use the current scene's verified,
// cached model resources; preview queries must not create game objects.
class WorldPlacementSurface
{
public:
    virtual ~WorldPlacementSurface() = default;
    virtual bool ModelReady(std::int16_t type) const = 0;
    virtual bool HeightAt(float x, float y, float& height) const = 0;
    // Minimum Z of the reference-pose geometry after rotation and scale.
    // The supplied record has Position={0,0,0}; no world translation is baked in.
    virtual bool MinimumModelZ(const WorldObjectRecord& transform, float& minimum) const = 0;
};
}
