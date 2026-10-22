#include "WorldCadUI.h"
#include "../core/WorldFileIo.h"
#include "../core/WorldModelCatalog.h"
#include "imgui.h"
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace Silver::WorldEditor
{
namespace
{
std::filesystem::path CadRoot(int mapId)
{
    if(mapId<0)throw std::invalid_argument("No hay mapa activo para CAD.");
    const auto root=std::filesystem::absolute(std::filesystem::path("EditorCandidates")/"CAD"/("Map"+std::to_string(mapId)));
    for(auto path=root;path!=path.root_path();path=path.parent_path())
        if(std::filesystem::exists(StreamPath(path))&&WorldModelCatalog::IsLinkedPath(path))
            throw std::invalid_argument("CAD no sigue enlaces ni junctions.");
    return root;
}
}
void WorldCadUI::Reset()
{ layout_=WorldCadLayout{};preview_.clear();saved_.clear();status_.clear();initialized_=false; }
void WorldCadUI::ClearPreview(IWorldEditorScene& scene)
{ preview_.clear();scene.SetPlacementPreviews({}); }

bool WorldCadUI::DrawParameters(IWorldEditorScene& scene)
{
    bool changed=false;int shape=static_cast<int>(layout_.Kind);
    if(ImGui::Combo("Forma",&shape,"Linea\0Polilinea\0Rectangulo\0Circulo\0"))
    {
        layout_.Kind=static_cast<WorldCadLayout::Shape>(shape);
        layout_.Points.resize(shape==3?1:2, {layout_.Points.front()[0]+100,layout_.Points.front()[1]+100});
        changed=true;
    }
    changed=ImGui::InputFloat("Separacion",&layout_.Spacing,10,100)||changed;
    changed=ImGui::Checkbox("Orientar segun recorrido",&layout_.AlignToPath)||changed;
    if(shape==3)changed=ImGui::InputFloat("Radio",&layout_.Radius,10,100)||changed;
    for(std::size_t i=0;i<layout_.Points.size();++i)
    {
        ImGui::PushID(static_cast<int>(i));
        changed=ImGui::InputFloat2("Punto XY",layout_.Points[i].data())||changed;
        ImGui::SameLine();
        if(ImGui::SmallButton("Camara"))
        {std::array<float,3> position{};if(scene.CameraMapPosition(position)){layout_.Points[i]={position[0],position[1]};changed=true;}}
        ImGui::PopID();
    }
    if(shape==1)
    {
        ImGui::BeginDisabled(layout_.Points.size()>=WorldCadLayout::MaximumPoints);
        if(ImGui::Button("Agregar punto")){auto point=layout_.Points.back();point[0]+=100;layout_.Points.push_back(point);changed=true;}
        ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(layout_.Points.size()<=2);
        if(ImGui::Button("Quitar ultimo")){layout_.Points.pop_back();changed=true;}
        ImGui::EndDisabled();
    }
    return changed;
}

void WorldCadUI::DrawSaved(int mapId)
{
    if(ImGui::Button("Guardar diseno CAD"))
    {
        const auto bytes=layout_.Encode(mapId);const auto root=CadRoot(mapId);
        std::filesystem::create_directories(StreamPath(root));
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        const auto target=root/("design-"+std::to_string(stamp)+".json");
        const auto pending=target.string()+".partial";
        if(std::filesystem::exists(StreamPath(target))||std::filesystem::exists(StreamPath(pending)))
            throw std::runtime_error("Ya existe ese diseno; no se sobrescribe.");
        {std::ofstream stream(StreamPath(pending),std::ios::binary);stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            if(!stream)throw std::runtime_error("No se pudo guardar el diseno CAD.");}
        std::filesystem::rename(StreamPath(pending),StreamPath(target));status_="Diseno guardado: "+PathUtf8(target.filename());
    }
    ImGui::SameLine();
    if(ImGui::Button("Buscar guardados"))
    {
        saved_.clear();const auto root=CadRoot(mapId);
        if(std::filesystem::is_directory(StreamPath(root)))
            for(const auto& file:std::filesystem::directory_iterator(StreamPath(root)))
            {if(saved_.size()>=128)break;if(file.is_regular_file()&&file.path().extension()==".json"&&!WorldModelCatalog::IsLinkedPath(file.path()))saved_.push_back(file.path());}
    }
    std::filesystem::path load;
    if(ImGui::BeginCombo("Cargar diseno","Elegir..."))
    {
        for(const auto& path:saved_)
            if(ImGui::Selectable(PathUtf8(path.filename()).c_str()))load=path;
        ImGui::EndCombo();
    }
    if(load.empty())return;
    (void)CadRoot(mapId);
    if(WorldModelCatalog::IsLinkedPath(load)||std::filesystem::file_size(StreamPath(load))>16384)
        throw std::runtime_error("Diseno CAD invalido.");
    std::ifstream stream(StreamPath(load),std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),{});
    layout_=WorldCadLayout::Decode(bytes,mapId);preview_.clear();status_="Diseno cargado; generar vista previa antes de confirmar.";
}

void WorldCadUI::Draw(WorldObjectDocument& document,IWorldEditorScene& scene,ObjectId selected,bool& open)
{
    if(!open){ClearPreview(scene);return;}
    ImGui::SetNextWindowSize({470,590},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("CAD / Distribuir objetos",&open))
    {
        try
        {
            const auto* prototype=document.Find(selected);
            if(!initialized_&&prototype)
            {const auto& p=prototype->Position;layout_.Points={{p[0],p[1]},{p[0]+1000,p[1]}};initialized_=true;}
            ImGui::TextWrapped("Selecciona un objeto existente como modelo. La altura y escala se conservan. 100 unidades = una celda MU.");
            if(DrawParameters(scene)||previewRevision_!=document.Revision())ClearPreview(scene);
            try{ImGui::Text("Recorrido: %.1f unidades",layout_.Length());}catch(const std::exception& e){ImGui::TextWrapped("%s",e.what());}
            ImGui::BeginDisabled(!prototype||prototype->BaselineOnly||document.HasPreview());
            const bool generate=ImGui::Button("Generar vista previa");
            ImGui::EndDisabled();ImGui::SameLine();
            if(generate){preview_=layout_.Generate(*prototype);previewRevision_=document.Revision();status_="Vista previa; todavia no modifica el mapa.";}
            if(ImGui::Button("Cancelar preview"))ClearPreview(scene);
            ImGui::Text("%zu objetos en preview",preview_.size());
            ImGui::BeginDisabled(preview_.empty()||document.HasPreview());
            const bool apply=ImGui::Button("Confirmar lote en Candidate");
            ImGui::EndDisabled();
            if(apply){document.AddMany(preview_,"CAD layout");ClearPreview(scene);status_="Lote agregado. Ctrl+Z revierte todo; guardar Candidate conserva el mapa.";}
            DrawSaved(scene.MapId());
            ImGui::TextWrapped("%s",status_.c_str());
        }
        catch(const std::exception& e){status_=e.what();ClearPreview(scene);ImGui::TextWrapped("%s",status_.c_str());}
    }
    ImGui::End();
    if(!open)ClearPreview(scene);else scene.SetPlacementPreviews(preview_);
}
}
