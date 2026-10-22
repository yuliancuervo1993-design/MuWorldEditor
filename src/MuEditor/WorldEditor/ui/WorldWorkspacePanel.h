#pragma once
#include "../core/WorldWorkspaceLayout.h"
#include "imgui.h"

namespace Silver::WorldEditor::Workspace
{
inline bool Begin(const char* title, WorldWorkspaceLayout& layout, WorkspacePanel panel)
{
    const auto display = ImGui::GetIO().DisplaySize;
    const auto rect = layout.DefaultRect(panel, display.x, display.y);
    auto& pending = layout.ResetPending[static_cast<std::size_t>(panel)];
    const bool reset = layout.ResetPositions || pending;
    pending = false;
    const auto condition = reset ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowPos({rect.X, rect.Y}, condition);
    ImGui::SetNextWindowSize({rect.Width, rect.Height}, condition);
    if (reset) ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({180.0f, 80.0f}, {display.x, display.y});
    return ImGui::Begin(title, &layout.Open(panel));
}
}
