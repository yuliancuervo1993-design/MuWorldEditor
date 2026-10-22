#include "WorldModelInspectionUI.h"
#include "../core/WorldFileIo.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace Silver::WorldEditor
{
namespace
{
constexpr float ThumbnailPadding = 0.85f;
constexpr float ProjectionDiagonal = 0.70710678f;
constexpr float ProjectionGround = 0.35f;
constexpr float ProjectionHeight = 0.85f;
constexpr ImU32 BackgroundColor = IM_COL32(18, 25, 32, 255);
constexpr ImU32 MeshColor = IM_COL32(132, 195, 219, 210);
const ImVec4 PresentColor{0.45f, 0.85f, 0.60f, 1.0f};
const ImVec4 ReviewColor{1.0f, 0.70f, 0.35f, 1.0f};
}
void WorldModelInspectionUI::Clear()
{
    selected_.clear(); error_.clear(); inspection_.reset(); thumbnail_.clear();
}
void WorldModelInspectionUI::Select(const std::string& relativePath)
{
    if (relativePath == selected_) return;
    Clear(); selected_ = relativePath;
    try { inspection_ = WorldModelInspection::Read("Data", relativePath); BuildThumbnail(); }
    catch (const std::exception& error) { error_ = error.what(); inspection_.reset(); thumbnail_.clear(); }
}
void WorldModelInspectionUI::SelectCandidate(const std::filesystem::path& bmdFile)
{
    const auto key = PathUtf8(std::filesystem::absolute(bmdFile).lexically_normal());
    if (key == selected_) return;
    Clear(); selected_ = key;
    try { inspection_ = WorldModelInspection::ReadCandidate(bmdFile); BuildThumbnail(); }
    catch (const std::exception& error) { error_ = error.what(); inspection_.reset(); thumbnail_.clear(); }
}
void WorldModelInspectionUI::BuildThumbnail()
{
    using Point = WorldModelThumbnail::Point;
    const auto maximum = std::numeric_limits<float>::max();
    Point low{maximum, maximum}, high{-maximum, -maximum};
    for (const auto& face : inspection_->Model.Geometry)
    {
        WorldModelThumbnail::Triangle triangle;
        for (std::size_t corner = 0; corner < triangle.size(); ++corner)
        {
            const auto& value = face[corner];
            triangle[corner] = {ProjectionDiagonal * (value[0] - value[1]),
                ProjectionGround * (value[0] + value[1]) - ProjectionHeight * value[2]};
            for (std::size_t axis = 0; axis < low.size(); ++axis)
            { low[axis] = std::min(low[axis], triangle[corner][axis]); high[axis] = std::max(high[axis], triangle[corner][axis]); }
        }
        thumbnail_.push_back(triangle);
    }
    const auto extent = std::max(high[0] - low[0], high[1] - low[1]);
    if (!std::isfinite(extent) || extent <= 0) { thumbnail_.clear(); return; }
    for (auto& face : thumbnail_) for (auto& point : face) for (std::size_t axis = 0; axis < point.size(); ++axis)
        point[axis] = 0.5f + (point[axis] - (low[axis] + high[axis]) * 0.5f) / extent * ThumbnailPadding;
}
void WorldModelInspectionUI::DrawThumbnail(float size) const
{
    const auto origin = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size, origin.y + size}, BackgroundColor);
    for (const auto& face : thumbnail_)
    {
        ImVec2 points[3];
        for (std::size_t corner = 0; corner < face.size(); ++corner)
            points[corner] = {origin.x + face[corner][0] * size, origin.y + face[corner][1] * size};
        draw->AddTriangle(points[0], points[1], points[2], MeshColor);
    }
    ImGui::Dummy({size, size});

}
void WorldModelInspectionUI::DrawDependencies() const
{
    using State = WorldModelInspection::TextureState;
    if (ImGui::BeginChild("dependencias-modelo", {0, 100}, ImGuiChildFlags_Borders))
    {
        for (const auto& texture : inspection_->Textures)
        {
            const bool present = texture.State == State::PackedFilePresent;
            ImGui::TextColored(present ? PresentColor : ReviewColor, "%s", texture.Name.c_str());
            const char* status = "Falta el archivo empaquetado";
            if (present) status = "Archivo empaquetado presente";
            else if (texture.State == State::RawOnly) status = "Solo existe JPG/TGA: el cliente necesita OZJ/OZT";
            else if (texture.State == State::UnsupportedName) status = "Nombre especial o no compatible: requiere revision";
            ImGui::TextWrapped("%s%s%s", status, texture.RelativePath.empty() ? "" : ": ", texture.RelativePath.c_str());
        }
    }
    ImGui::EndChild();
}
void WorldModelInspectionUI::Draw() const
{
    if (!error_.empty()) { ImGui::TextWrapped("No se previsualiza: %s", error_.c_str()); return; }
    if (!inspection_) return;
    const auto& model = inspection_->Model;
    ImGui::Separator(); DrawThumbnail(); ImGui::SameLine(); ImGui::BeginGroup();
    ImGui::Text("BMD %d | %d mallas", model.Version, model.MeshCount);
    ImGui::Text("%zu vertices | %zu triangulos", model.VertexCount, model.TriangleCount);
    ImGui::Text("%d huesos | %d acciones", model.BoneCount, model.ActionCount);
    ImGui::TextUnformatted("Pose inicial del modelo; sin materiales ni equipo adicional.");
    ImGui::TextUnformatted("Lectura solamente: no ocupa slots del mapa.");
    ImGui::EndGroup(); DrawDependencies();
    ImGui::TextWrapped("La presencia del archivo no aprueba su imagen ni habilita importarlo. No se modifico el mapa.");
}
}
