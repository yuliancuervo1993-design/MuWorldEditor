#pragma once
#include <array>

namespace Silver::WorldEditor
{
struct WorldEditorCamera
{
    std::array<float, 16> View{};
    std::array<float, 16> Projection{};
    float Width = 0, Height = 0;
    static WorldEditorCamera FromClient(const float view[3][4], float perspectiveX, float perspectiveY,
        float centerX, float centerY, float pixelWidth, float pixelHeight, float nearPlane, float farPlane,
        float logicalWidth, float logicalHeight);
    bool Project(const std::array<float, 3>& point, std::array<float, 2>& result) const;
};
}
