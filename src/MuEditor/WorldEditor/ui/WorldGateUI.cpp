#include "WorldGateUI.h"
#include "imgui.h"
#include <stdexcept>
#include <unordered_map>

namespace Silver::WorldEditor
{
void WorldGateUI::Read(const WorldGate& gate, std::uint64_t revision)
{
    selected_=gate.Id; revision_=revision;
    entry_={gate.X1,gate.Y1,gate.X2,gate.Y2}; exit_={gate.TargetX1,gate.TargetY1,gate.TargetX2,gate.TargetY2};
    level_=gate.LevelRequirement; map_=gate.TargetMap; target_=gate.TargetGateId; direction_=gate.TargetDirection;
}

WorldGate WorldGateUI::Value(const WorldGate& original) const
{
    for (const auto& area:{entry_,exit_})
        for (const auto coordinate:area)
            if (coordinate<0 || coordinate>255) throw std::invalid_argument("Coordenadas Gate: 0 a 255.");
    if (level_<0 || level_>32767 || map_<0 || direction_<0 || direction_>7 || target_==NoGate)
        throw std::invalid_argument("Nivel, destino o direccion fuera de rango.");
    auto gate=original;
    gate.X1=static_cast<std::uint8_t>(entry_[0]); gate.Y1=static_cast<std::uint8_t>(entry_[1]);
    gate.X2=static_cast<std::uint8_t>(entry_[2]); gate.Y2=static_cast<std::uint8_t>(entry_[3]);
    gate.TargetX1=static_cast<std::uint8_t>(exit_[0]); gate.TargetY1=static_cast<std::uint8_t>(exit_[1]);
    gate.TargetX2=static_cast<std::uint8_t>(exit_[2]); gate.TargetY2=static_cast<std::uint8_t>(exit_[3]);
    gate.LevelRequirement=static_cast<std::int16_t>(level_); gate.TargetMap=map_; gate.TargetGateId=target_;
    gate.TargetDirection=static_cast<std::uint8_t>(direction_); WorldGateDocument::Validate(gate); return gate;
}

void WorldGateUI::Draw(WorldGateDocument& document, GateId selected)
{
    const auto* gate=document.Find(selected);
    if (!gate) { selected_=NoGate; ImGui::TextDisabled("Gate no disponible."); return; }
    if (selected_!=selected || revision_!=document.Revision()) Read(*gate,document.Revision());
    ImGui::Text("Gate %d | ID %llu",gate->Number,static_cast<unsigned long long>(gate->Id));
    ImGui::InputInt4("Entrada X1 Y1 X2 Y2",entry_.data());
    ImGui::InputInt("Nivel requerido",&level_);
    ImGui::InputInt("Mapa destino",&map_);
    ImGui::InputScalar("Gate destino ID",ImGuiDataType_U64,&target_);
    ImGui::InputInt4("Salida X1 Y1 X2 Y2",exit_.data());
    ImGui::InputInt("Direccion (0..7)",&direction_);
    if (ImGui::Button("Aplicar Gate a Candidate"))
    {
        try { if (document.SetCandidate(selected,Value(*gate))) status_="Gate editado; Ctrl+Z revierte. Guardar Candidate para conservar."; }
        catch (const std::exception& error) { status_=error.what(); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Descartar formulario")) { Read(*gate,document.Revision()); status_.clear(); }
    ImGui::TextWrapped("%s",status_.c_str());
    ImGui::TextWrapped("Edicion local de Gates existentes. El export OpenMU se conserva; no se aplica al servidor. Check Map revisa referencias conocidas.");
}

void WorldGateUI::DrawChanges(const WorldGateDocument& document)
{
    if (!document.Loaded() || !ImGui::TreeNode("Gates: cambios respecto al export OpenMU")) return;
    if (reviewRevision_!=document.Revision())
    {
        changes_.clear();
        std::unordered_map<GateId,const WorldGate*> baseline;
        for (const auto& gate:document.BaselineEntries()) baseline.emplace(gate.Id,&gate);
        for (const auto& gate:document.Entries())
        {
            const auto found=baseline.find(gate.Id);
            if (found==baseline.end() || gate==*found->second) continue;
            const auto& before=*found->second;
            std::string text="Gate "+std::to_string(gate.Number)+" (ID "+std::to_string(gate.Id)+")";
            if (gate.LevelRequirement!=before.LevelRequirement) text+=" | nivel "+std::to_string(before.LevelRequirement)+" -> "+std::to_string(gate.LevelRequirement);
            if (gate.X1!=before.X1 || gate.Y1!=before.Y1 || gate.X2!=before.X2 || gate.Y2!=before.Y2) text+=" | entrada modificada";
            if (gate.TargetGateId!=before.TargetGateId || gate.TargetMap!=before.TargetMap) text+=" | destino "+std::to_string(before.TargetMap)+":"+std::to_string(before.TargetGateId)+" -> "+std::to_string(gate.TargetMap)+":"+std::to_string(gate.TargetGateId);
            if (gate.TargetX1!=before.TargetX1 || gate.TargetY1!=before.TargetY1 || gate.TargetX2!=before.TargetX2 || gate.TargetY2!=before.TargetY2 || gate.TargetDirection!=before.TargetDirection) text+=" | salida/direccion modificada";
            changes_.push_back(std::move(text));
        }
        reviewRevision_=document.Revision();
    }
    ImGui::Text("%zu Gates modificados; export original conservado.",changes_.size());
    constexpr std::size_t MaximumVisibleChanges=128;
    for (std::size_t index=0;index<changes_.size() && index<MaximumVisibleChanges;++index)
        ImGui::BulletText("%s",changes_[index].c_str());
    if (changes_.size()>MaximumVisibleChanges) ImGui::TextDisabled("Lista limitada a 128; el Candidate guarda todos.");
    ImGui::TreePop();
}
}
