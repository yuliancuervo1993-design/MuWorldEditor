#include "WorldEffectsUI.h"
#include "imgui.h"
#include <array>

namespace Silver::WorldEditor
{
void WorldEffectsUI::Reset()
{
    selected_ = NoEffect;
    editingId_ = NoEffect;
    editing_ = {};
    status_.clear();
}

void WorldEffectsUI::Select(const WorldPointLight* light)
{
    if (!light)
    {
        selected_ = NoEffect;
        editingId_ = NoEffect;
        editing_ = {};
        return;
    }
    selected_ = light->Id;
    editingId_ = light->Id;
    editing_ = *light;
}

bool WorldEffectsUI::Draw(WorldEffectDocument& document, IWorldEditorScene& scene, bool* open, bool resetLayout)
{
    bool save = false;
    ImGui::SetNextWindowPos({8, 148}, resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({380, 520}, resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Efectos y luces | adapters soportados", open))
    {
        ImGui::TextWrapped("Adapter activo: Luz puntual de terreno. Usa AddTerrainLight del cliente real; se guarda como sidecar candidate y nunca modifica Data.");
        ImGui::TextDisabled("Fuego, humo, particulas, portales y agua se agregan como adapters separados cuando su representacion runtime quede validada.");

        ImGui::TextDisabled("Undo/Redo: History global (Ctrl+Z / Ctrl+Y).");
        if (ImGui::Button("Guardar efectos candidate")) save = true;

        ImGui::Text("%zu luces | %s", document.Lights().size(), document.Dirty() ? "cambios sin guardar" : "sin cambios");

        std::array<float, 3> camera{};
        const bool cameraAvailable = scene.CameraMapPosition(camera);
        ImGui::BeginDisabled(!cameraAvailable);
        if (ImGui::Button("Agregar luz en la camara"))
        {
            try
            {
                const auto id = document.AddPointLight(camera);
                Select(document.Find(id));
                status_ = "Luz agregada. Ajusta posicion/color/radio y aplica cambios.";
            }
            catch (const std::exception& error) { status_ = error.what(); }
        }
        ImGui::EndDisabled();
        if (!cameraAvailable) ImGui::TextDisabled("La camara del mapa no esta disponible.");

        ImGui::SeparatorText("Luces del mapa");
        if (ImGui::BeginChild("world-effects-list", {0, 150}, ImGuiChildFlags_Borders))
        {
            for (const auto& light : document.Lights())
            {
                const std::string label = "#" + std::to_string(light.Id)
                    + " | X " + std::to_string(static_cast<int>(light.Position[0]))
                    + " Y " + std::to_string(static_cast<int>(light.Position[1]))
                    + " | R " + std::to_string(light.Radius);
                if (ImGui::Selectable(label.c_str(), selected_ == light.Id))
                    Select(&light);
            }
        }
        ImGui::EndChild();

        const auto* selected = document.Find(selected_);
        if (!selected)
        {
            Select(nullptr);
            ImGui::TextWrapped("Selecciona una luz o agrega una en la posicion actual de la camara.");
        }
        else
        {
            if (editingId_ != selected->Id) Select(selected);
            ImGui::SeparatorText("Inspector de luz");
            ImGui::InputFloat3("Posicion XYZ", editing_.Position.data(), "%.1f");
            ImGui::ColorEdit3("Color", editing_.Color.data());
            ImGui::SliderFloat("Intensidad", &editing_.Intensity, 0.0f, 4.0f, "%.2f");
            ImGui::SliderInt("Radio (celdas)", &editing_.Radius, 1, 32);

            if (ImGui::Button("Aplicar cambios"))
            {
                try
                {
                    if (document.UpdatePointLight(selected_, editing_))
                        status_ = "Cambios de luz aplicados.";
                }
                catch (const std::exception& error) { status_ = error.what(); }
                Select(document.Find(selected_));
            }
            ImGui::SameLine();
            if (ImGui::Button("Recargar valores"))
                Select(document.Find(selected_));
            ImGui::SameLine();
            if (ImGui::Button("Eliminar luz"))
            {
                if (document.Remove(selected_))
                {
                    status_ = "Luz eliminada.";
                    Select(nullptr);
                }
            }
        }

        if (!status_.empty())
        {
            ImGui::SeparatorText("Estado");
            ImGui::TextWrapped("%s", status_.c_str());
        }
    }
    ImGui::End();
    return save;
}
}
