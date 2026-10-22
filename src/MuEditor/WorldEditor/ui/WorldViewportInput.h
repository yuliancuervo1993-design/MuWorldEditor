#pragma once
#include "imgui.h"
namespace Silver::WorldEditor
{
struct WorldCameraPointer { bool Held=false; float X=0, Y=0; };
inline WorldCameraPointer ReadWorldCameraPointer(bool rightLookEnabled)
{
    if (!ImGui::GetCurrentContext()) return {};
    const auto& io=ImGui::GetIO();
    if (io.AppFocusLost || io.WantCaptureMouse || io.WantTextInput
        || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) return {};
    return {io.MouseDown[ImGuiMouseButton_Middle]
        || (rightLookEnabled && io.MouseDown[ImGuiMouseButton_Right]),io.MousePos.x,io.MousePos.y};
}
}
