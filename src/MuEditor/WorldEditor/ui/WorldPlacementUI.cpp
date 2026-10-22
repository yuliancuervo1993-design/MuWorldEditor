#include "WorldPlacementUI.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace Silver::WorldEditor
{
namespace { constexpr float ScaleStep = 10.0f; }
void WorldPlacementUI::Choose(const WorldObjectRecord& prototype,
    const WorldObjectDocument& document, IWorldEditorScene& scene)
{
    error_.clear();
    if (!scene.PlacementSurface() || !scene.SceneIdentity())
    { error_ = "La escena no ofrece colocacion provisional."; return; }
    placement_.Select(prototype, document, scene.SceneIdentity());
    placement_.SetSupport(usePivot_ ? WorldPlacementSession::Support::Pivot : WorldPlacementSession::Support::ModelBase);
}
const char* WorldPlacementUI::StatusText() const
{
    using S = WorldPlacementSession::Status;
    switch (placement_.CurrentStatus())
    {
    case S::Ready: return "Listo para colocar - clic izquierdo";
    case S::OutsideMap: return "No se puede colocar: fuera del terreno";
    case S::ModelUnavailable: return "No se puede colocar: revisar modelo, pose o texturas";
    case S::SurfaceUnavailable: case S::InvalidSurface: return "No se puede colocar: suelo no disponible";
    case S::ObjectLimit: return "Limite de objetos alcanzado";
    default: return "Move el cursor sobre el mapa para ver la copia";
    }
}
void WorldPlacementUI::Controls()
{
    if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
    if (!Active()) return;
    ImGui::SeparatorText("Copia por colocar (todavia no guardada)");
    if (ImGui::Button("Girar izquierda [Q]")) placement_.RotateSteps(-1);
    ImGui::SameLine(); if (ImGui::Button("Derecha [E]")) placement_.RotateSteps(1);
    ImGui::Text("Giro: %.0f grados", placement_.Transform().Angle[2]);
    const auto [low, high] = placement_.ScalePercentRange();
    float percent = std::clamp(placement_.ScalePercent(), low, high);
    bool changed = false;
    if (ImGui::Button("-##escala")) { percent -= ScaleStep; changed = true; }
    ImGui::SameLine(); if (ImGui::Button("+##escala")) { percent += ScaleStep; changed = true; }
    ImGui::SameLine(); if (ImGui::Button("Restablecer 100%")) { percent = 100.0f; changed = true; }
    changed = ImGui::SliderFloat("Tamano (%)", &percent, low, high, "%.0f%%") || changed;
    if (changed) placement_.SetScalePercent(std::clamp(percent, low, high));
    bool grid = placement_.GridEnabled(), repeat = placement_.Repeating();
    if (ImGui::Checkbox("Ajustar a grilla [G]", &grid)) placement_.SetGrid(grid);
    if (ImGui::Checkbox("Colocar varias copias", &repeat)) placement_.SetRepeat(repeat);
    if (ImGui::Checkbox("Usar pivote (sin apoyar la base)", &usePivot_))
        placement_.SetSupport(usePivot_ ? WorldPlacementSession::Support::Pivot : WorldPlacementSession::Support::ModelBase);
    ImGui::TextWrapped("%s", StatusText());
    ImGui::TextWrapped("Clic: colocar | Rueda: zoom | Ctrl + rueda: tamano | Esc / clic derecho: cancelar");
    if (ImGui::Button("Cancelar colocacion")) Cancel();
}
void WorldPlacementUI::Shortcuts()
{
    const auto& io = ImGui::GetIO();
    if (io.KeyAlt || io.WantTextInput) return;

    if (!io.KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) placement_.RotateSteps(-1);
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) placement_.RotateSteps(1);
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) placement_.SetGrid(!placement_.GridEnabled());
    }

    if (std::isfinite(io.MouseWheel) && io.MouseWheel != 0.0f)
    {
        if (io.KeyCtrl)
        {
            const auto [low, high] = placement_.ScalePercentRange();
            placement_.SetScalePercent(std::clamp(
                placement_.ScalePercent() + io.MouseWheel * ScaleStep, low, high));
        }
        // Plain wheel is handled once at session level as camera zoom.
    }
}
void WorldPlacementUI::DrawTarget(IWorldEditorScene& scene) const
{
    const auto& preview = placement_.Preview();
    auto* draw = ImGui::GetBackgroundDrawList();
    const auto color = preview ? IM_COL32(90,255,160,240) : IM_COL32(255,100,85,240);
    const auto mouse = ImGui::GetMousePos();
    draw->AddText({mouse.x + 18.0f, mouse.y + 18.0f}, color, StatusText());
    if (!preview) return;
    std::array<float,2> center{};
    if (!scene.Camera().Project(preview->Position, center)) return;
    draw->AddCircle({center[0],center[1]}, 9.0f, color, 24, 2.0f);
    draw->AddLine({center[0]-14,center[1]}, {center[0]+14,center[1]}, color, 2.0f);
    draw->AddLine({center[0],center[1]-14}, {center[0],center[1]+14}, color, 2.0f);
}
bool WorldPlacementUI::Update(WorldObjectDocument& document, IWorldEditorScene& scene, ObjectId& selected)
{
    scene.SetPlacementPreview(std::nullopt);
    if (!Active()) return false;
    const auto& io = ImGui::GetIO();
    if (io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    { Cancel(); return true; }
    if (io.WantCaptureMouse || io.WantTextInput || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || ImGui::IsAnyItemActive())
    { placement_.Suspend(); return true; }
    const auto* surface = scene.PlacementSurface();
    if (!surface) { Cancel(); return true; }
    try
    {
        Shortcuts();
        std::array<float,3> hit{}; const auto mouse = ImGui::GetMousePos();
        if (!scene.Placement(mouse.x, mouse.y, hit)) hit[0] = hit[1] = std::numeric_limits<float>::quiet_NaN();
        placement_.UpdateTarget(hit[0], hit[1], document, scene.SceneIdentity(), *surface);
        const auto added = placement_.PrimaryButton(ImGui::IsMouseDown(ImGuiMouseButton_Left), document, scene.SceneIdentity(), *surface);
        if (added != NoObject) selected = added;
        if (Active()) { scene.SetPlacementPreview(placement_.Preview()); DrawTarget(scene); }
    }
    catch (const std::exception& error) { error_ = error.what(); Cancel(); }
    return true;
}
}
