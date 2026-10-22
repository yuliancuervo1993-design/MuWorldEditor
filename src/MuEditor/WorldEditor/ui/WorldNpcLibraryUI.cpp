#include "WorldNpcLibraryUI.h"
#include "../core/WorldModelCatalog.h"
#include "../core/WorldNpcVisualBindings.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
namespace Silver::WorldEditor
{
void WorldNpcLibraryUI::Reload()
{
    loaded_=true;selected_=-1;placing_=false;previewNumber_=-1;inspection_.Clear();
    catalog_={};filtered_.clear();
    try
    {
        const std::filesystem::path path="EditorCandidates/Gameplay/npc-catalog.json";
        if(WorldModelCatalog::IsLinkedPath(path)||std::filesystem::file_size(path)>2*1024*1024)
            throw std::runtime_error("Catalogo NPC no valido.");
        std::ifstream input(path,std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
        catalog_.Load(bytes);Filter();status_.clear();
    }
    catch(const std::exception& error){status_=error.what();}
}
void WorldNpcLibraryUI::Draw(WorldMonsterSpawnDocument& document,bool* open)
{
    if(!loaded_)Reload();
    ImGui::SetNextWindowSize({460,600},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("NPC / Mobs | servidor LOCAL",open))
    {
        ImGui::Text("%zu definiciones del servidor",catalog_.Entries().size());
        ImGui::TextWrapped("Lectura: %s",catalog_.ExportedUtc().c_str());
        ImGui::TextWrapped("Coloca spawns del catalogo real en el mapa. Se guardan como propuesta local; no activa cambios en el servidor.");
        const bool searched=ImGui::InputTextWithHint("##buscar-npc","Nombre o numero del servidor",search_,sizeof(search_));
        constexpr const char* kinds[]{"Todos","NPC y guardias","Monstruos","Otros del servidor"};
        if(ImGui::Combo("Tipo",&kind_,kinds,4)||searched)Filter();
        ImGui::InputInt("Cantidad",&quantity_);quantity_=(std::clamp)(quantity_,1,1000);
        ImGui::SliderInt("Direccion (0 automatica)",&direction_,0,8);
        ImGui::Checkbox("Colocar varias copias",&repeat_);
        const auto& variants=document.MapVariants();
        if(variant_>=static_cast<int>(variants.size()))variant_=0;
        if(!variants.empty() && ImGui::BeginCombo("Mapa del servidor",variants[static_cast<std::size_t>(variant_)].second.c_str()))
        {
            for(std::size_t i=0;i<variants.size();++i)
                if(ImGui::Selectable(variants[i].second.c_str(),variant_==static_cast<int>(i)))variant_=static_cast<int>(i);
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(selected_<0||!document.Loaded());
        if(ImGui::Button("Colocar NPC / Mob"))
        {
            BeginPlacement(document);
        }
        ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Recargar catalogo"))Reload();
        if(!document.Loaded())ImGui::TextWrapped("Falta el export de spawns del mapa actual.");
        if(!status_.empty())ImGui::TextWrapped("%s",status_.c_str());
        DrawSelection();DrawCatalog();
    }
    ImGui::End();
}
void WorldNpcLibraryUI::BeginPlacement(const WorldMonsterSpawnDocument& document)
{
    prototype_=catalog_.Entries().at(static_cast<std::size_t>(selected_));
    const auto& variants=document.MapVariants();
    if(!variants.empty())prototype_.MapDefinitionId=variants.at(static_cast<std::size_t>(variant_)).first;
    prototype_.Quantity=static_cast<std::int16_t>(quantity_);
    prototype_.Direction=static_cast<std::uint8_t>(direction_);
    Choose(prototype_);
    status_="Clic coloca; Shift conserva la herramienta; Q/E cambia direccion. Esc cancela.";
}
void WorldNpcLibraryUI::Filter()
{
    filtered_.clear();const auto search=WorldModelCatalog::Lower(search_);
    for(std::size_t i=0;i<catalog_.Entries().size();++i)
    {
        const auto& row=catalog_.Entries()[i];
        const bool npc=row.ObjectKind=="PassiveNpc"||row.ObjectKind=="Guard";
        const bool monster=row.ObjectKind=="Monster";
        if((kind_==1&&!npc)||(kind_==2&&!monster)||(kind_==3&&(npc||monster)))continue;
        const auto label=std::to_string(row.MonsterNumber)+" | "+row.Designation+" | "+row.ObjectKind;
        if(search.empty()||WorldModelCatalog::Lower(label).find(search)!=std::string::npos)filtered_.push_back(i);
    }
    if(selected_>=0&&std::find(filtered_.begin(),filtered_.end(),static_cast<std::size_t>(selected_))==filtered_.end())
    {selected_=-1;previewNumber_=-1;inspection_.Clear();Cancel();}
}
void WorldNpcLibraryUI::DrawCatalog()
{
    ImGui::Text("%zu resultados",filtered_.size());
    if(ImGui::BeginChild("npc-catalogo",{0,0},ImGuiChildFlags_Borders))
    {
        ImGuiListClipper clipper;clipper.Begin(static_cast<int>(filtered_.size()));
        while(clipper.Step())for(int index=clipper.DisplayStart;index<clipper.DisplayEnd;++index)
        {
            const auto i=filtered_[static_cast<std::size_t>(index)];const auto& row=catalog_.Entries()[i];
            const auto label=std::to_string(row.MonsterNumber)+" | "+row.Designation+" | "+row.ObjectKind;
            if(ImGui::Selectable(label.c_str(),selected_==static_cast<int>(i)))
            {selected_=static_cast<int>(i);Cancel();}
        }
    }
    ImGui::EndChild();
}
void WorldNpcLibraryUI::DrawSelection()
{
    if(selected_<0)return;
    const auto& row=catalog_.Entries().at(static_cast<std::size_t>(selected_));
    ImGui::TextWrapped("Seleccion: %s (#%d)",row.Designation.c_str(),row.MonsterNumber);
    if(!ImGui::CollapsingHeader("Vista previa del modelo base",ImGuiTreeNodeFlags_DefaultOpen))return;
    const auto path=WorldNpcVisualPath(row.MonsterNumber);
    if(path.empty())
    {ImGui::TextWrapped("Modelo compuesto o sin correspondencia verificada. Puede colocarse por nombre e ID.");return;}
    if(previewNumber_!=row.MonsterNumber)
    {previewNumber_=row.MonsterNumber;inspection_.SelectCandidate(std::filesystem::path("Data")/path);}
    if(ImGui::BeginChild("npc-modelo",{0,180},ImGuiChildFlags_Borders))
    {
        ImGui::TextWrapped("%s",std::string(path).c_str());
        inspection_.Draw();
    }
    ImGui::EndChild();
}
void WorldNpcLibraryUI::Update(WorldMonsterSpawnDocument& document,IWorldEditorScene& scene,SpawnId& selected)
{
    if(!placing_)return;
    const auto& io=ImGui::GetIO();
    if(!placementScene_)placementScene_=scene.SceneIdentity();
    if(!document.Loaded()||*placementScene_!=scene.SceneIdentity()){Cancel();return;}
    if(io.AppFocusLost||ImGui::IsKeyPressed(ImGuiKey_Escape)||ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {Cancel();return;}
    if(io.WantCaptureMouse||io.WantTextInput||ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))return;
    if(io.MouseDown[ImGuiMouseButton_Middle]||io.MouseDown[ImGuiMouseButton_Right])return;
    const int step=ImGui::IsKeyPressed(ImGuiKey_E,false)?1:ImGui::IsKeyPressed(ImGuiKey_Q,false)?-1:0;
    if(step)prototype_.Direction=static_cast<std::uint8_t>(prototype_.Direction==0?1:1+(prototype_.Direction-1+step+8)%8);
    std::array<float,3> position{};
    if(!scene.Placement(io.MousePos.x,io.MousePos.y,position))return;
    if(!std::isfinite(position[0])||!std::isfinite(position[1])||position[0]<0||position[1]<0||position[0]>=25600||position[1]>=25600)return;
    prototype_.X1=prototype_.X2=static_cast<std::uint8_t>(position[0]/100);
    prototype_.Y1=prototype_.Y2=static_cast<std::uint8_t>(position[1]/100);
    auto* draw=ImGui::GetBackgroundDrawList();
    draw->AddCircle(io.MousePos,12,IM_COL32(100,255,190,255),24,2);
    const auto label=prototype_.Designation+" | "+std::to_string(prototype_.X1)+","+std::to_string(prototype_.Y1)+" | dir "+std::to_string(prototype_.Direction)+" | x"+std::to_string(prototype_.Quantity);
    draw->AddText({io.MousePos.x+18,io.MousePos.y+12},IM_COL32(100,255,190,255),label.c_str());
    if(!ImGui::IsMouseClicked(ImGuiMouseButton_Left))return;
    try
    {
        selected=document.AddCandidate(prototype_);direction_=prototype_.Direction;
        status_="Spawn colocado en la propuesta local. Ctrl+S guarda; Ctrl+Z deshace.";
        if(!repeat_&&!io.KeyShift)Cancel();
    }
    catch(const std::exception& error){status_=error.what();Cancel();}
}
}
