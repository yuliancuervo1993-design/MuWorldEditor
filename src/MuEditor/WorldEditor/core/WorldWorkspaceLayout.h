#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace Silver::WorldEditor
{
enum class WorkspacePanel : std::size_t { Scene, Inspector, Assets, Details, Count };

struct WorldWorkspaceLayout
{
    struct Rect { float X, Y, Width, Height; };
    static constexpr float RibbonTop = 40.0f;
    static constexpr float RibbonHeight = 100.0f;
    static constexpr float StatusHeight = 28.0f;
    static constexpr float Gap = 8.0f;
    std::array<bool, 4> Panels{true, true, false, false};
    std::array<bool, 4> ResetPending{};
    bool FocusMode = false;
    bool ResetPositions = false;
    bool RibbonCollapsed = false;

    bool& Open(WorkspacePanel panel) { return Panels[static_cast<std::size_t>(panel)]; }
    bool Visible(WorkspacePanel panel) const { return !FocusMode && Panels[static_cast<std::size_t>(panel)]; }
    float Top() const { return RibbonTop + (RibbonCollapsed ? 38.0f : RibbonHeight) + Gap; }
    void Restore() { Panels = {true, true, false, false}; FocusMode = false; ResetPositions = true; ResetPending.fill(true); }

    Rect DefaultRect(WorkspacePanel panel, float width, float height) const
    {
        width = std::max(width, 320.0f); height = std::max(height, 240.0f);
        const float side = std::clamp(width * 0.19f, 220.0f, 320.0f);
        const float bottom = std::max(Top() + 80.0f, height - StatusHeight - Gap);
        const float assets = std::clamp(height * 0.23f, 140.0f, 230.0f);
        const float body = std::max(80.0f, bottom - Top() - (Visible(WorkspacePanel::Assets) ? assets + Gap : 0.0f));
        if (panel == WorkspacePanel::Assets) return {Gap, bottom - assets, width - Gap * 2, assets};
        const bool right = panel == WorkspacePanel::Inspector || panel == WorkspacePanel::Details;
        return {right ? width - side - Gap : Gap, Top(), side, body};
    }
};
}
