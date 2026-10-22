#include "WorldEditorUI.h"
#include "WorldCameraViews.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Silver::WorldEditor;
namespace
{
int checks = 0;
void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
    ++checks;
}
struct Scene final : IWorldEditorScene
{
    std::vector<WorldPaletteItem> Items;
    WorldEditorCamera Camera() const override { return {}; }
    ObjectId Pick(float, float) override { return NoObject; }
    bool Placement(float, float, std::array<float, 3>&) override { return false; }
    bool Bounds(ObjectId, std::array<std::array<float, 3>, 8>&) const override { return false; }
    const std::vector<WorldPaletteItem>& Palette() const override { return Items; }
    void Synchronize(const WorldObjectDocument&) override {}
};
void CameraViews()
{
    const std::array<float, 3> target{12000, 14000, 150};
    for (int view = 0; view < 4; ++view)
    {
        const auto pose = WorldCameraViews::FromTarget(view, target, 2200);
        Check(pose.has_value(), "valid view");
        const float y = pose->Yaw * WorldCameraViews::Pi / 180.0f;
        const float p = pose->Pitch * WorldCameraViews::Pi / 180.0f;
        const std::array<float, 3> ray{-std::sin(y)*std::sin(p), -std::cos(y)*std::sin(p), -std::cos(p)};
        for (std::size_t axis = 0; axis < 3; ++axis)
            Check(std::abs(pose->Position[axis] + ray[axis]*2200 - target[axis]) < 0.01f, "all views look at original target");
        if (view == 1) Check(ray[2] == -1.0f, "top view actually looks down");
        if (view >= 2) Check(std::abs(ray[2]) < 0.0001f, "front and side actually horizontal");
    }
    Check(!WorldCameraViews::FromTarget(-1, target, 2200), "invalid view rejected");
    Check(!WorldCameraViews::FromTarget(0, target, 0), "invalid distance rejected");
    Check(!WorldCameraViews::FromTarget(0, {NAN, 0, 0}, 2200), "nonfinite target rejected");
}
void Panels()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = {1280, 720}; io.DeltaTime = 1.0f/60;
    unsigned char* pixels = nullptr; int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Scene scene; WorldObjectDocument document; WorldEditorUI ui;
    const auto frame = [&] { ImGui::NewFrame(); ui.Draw(document, scene); ImGui::Render(); };
    frame(); frame();
    auto* inspector = ImGui::FindWindowByName("Propiedades###WorldInspector");
    Check(inspector && inspector->Active, "inspector appears by default");
    Check(!(inspector->Flags & ImGuiWindowFlags_NoMove) && !(inspector->Flags & ImGuiWindowFlags_NoResize), "panel can move and resize");
    ui.WorkspaceLayout().Open(WorkspacePanel::Inspector) = false; frame();
    Check(!inspector->Active, "closed panel does not cover map");
    ui.WorkspaceLayout().Open(WorkspacePanel::Inspector) = true; frame();
    Check(inspector->Active, "closed panel can be restored");
    ImGui::SetWindowCollapsed("Propiedades###WorldInspector", true); frame();
    Check(inspector->Collapsed, "minimized panel stays minimized");
    ui.WorkspaceLayout().FocusMode = true; frame();
    Check(!inspector->Active, "focus mode hides panels");
    ui.WorkspaceLayout().FocusMode = false; frame();
    Check(inspector->Active && inspector->Collapsed, "focus mode preserves user layout");
    ui.WorkspaceLayout().Restore(); frame();
    Check(!inspector->Collapsed, "reset recovers minimized panel");
    Check(!document.Dirty(), "workspace changes never mutate map");
    ImGui::DestroyContext();
}
void Layouts()
{
    for (const auto width : {1024.0f,1280.0f,1920.0f,2560.0f})
        for (const auto height : {600.0f,720.0f,1080.0f})
        {
            WorldWorkspaceLayout layout;
            const auto left = layout.DefaultRect(WorkspacePanel::Scene, width, height);
            const auto right = layout.DefaultRect(WorkspacePanel::Inspector, width, height);
            Check(right.X - left.X - left.Width >= width * 0.35f, "default preserves useful viewport width");
            Check(left.Y + left.Height <= height - WorldWorkspaceLayout::StatusHeight, "panels leave telemetry visible");
        }
}
}
int main()
{
    try { CameraViews(); Layouts(); Panels(); std::cout << checks << " workspace/camera checks passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
