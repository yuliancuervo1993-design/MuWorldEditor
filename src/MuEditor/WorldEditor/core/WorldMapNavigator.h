#pragma once
#include "WorldObjectRecord.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Silver::WorldEditor
{
struct WorldMapNavigator
{
    static bool WorldToNormalized(float x, float y, std::array<float, 2>& normalized)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || y < 0.0f || x >= MapEdge || y >= MapEdge)
            return false;
        normalized = {x / MapEdge, 1.0f - y / MapEdge};
        return true;
    }

    static bool NormalizedToWorld(float x, float y, std::array<float, 2>& world)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || y < 0.0f || x > 1.0f || y > 1.0f)
            return false;
        const float maximum = std::nextafter(MapEdge, 0.0f);
        world = {std::min(x * MapEdge, maximum), std::min((1.0f - y) * MapEdge, maximum)};
        return true;
    }
};
}
