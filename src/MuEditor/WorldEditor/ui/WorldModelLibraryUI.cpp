#include "WorldModelLibraryUI.h"
#include "../core/WorldFileIo.h"
#include "imgui.h"
#include "json.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <set>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif
namespace Silver::WorldEditor
{
namespace
{
constexpr const char* Categories[]{"Todos","Arboles","Vegetacion","Edificios","Muros y puertas","Rocas y terreno","Muebles","Puentes y caminos","Luces y decoracion","Agua y efectos","Ruinas y estructuras","Items decorativos","Equipamiento","Sin clasificar"};
constexpr const char* TerrainCategories[]{"Todos","Cesped","Suelo","Agua","Madera","Roca","Extendido","Especial"};
constexpr const char* SilverHdStates[]{"Todos","ORIGINAL","REMAKE","LOD","CANDIDATE","ROLLBACK","SIN CLASIFICAR"};
bool BlenderImportReady(const std::filesystem::path& workspace)
{
    const auto path = StreamPath(workspace / "bridge-state.json");
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 4096) return false;
    std::ifstream input(path, std::ios::binary);
    const auto state = nlohmann::json::parse(input, nullptr, false);
    return state.is_object() && state.value("status", "") == "IMPORTED" && state.value("editor", false);
}
#ifdef _WIN32
std::filesystem::path BlenderBridgePath(const wchar_t* variable)
{
    const auto required = GetEnvironmentVariableW(variable, nullptr, 0);
    if (required == 0)
        throw std::runtime_error("Puente Blender no configurado. Use el iniciador local del editor con Blender instalado.");
    std::vector<wchar_t> value(required);
    const auto written = GetEnvironmentVariableW(variable, value.data(), required);
    if (written == 0 || written >= required)
        throw std::runtime_error("La configuracion de Blender cambio durante la lectura.");
    const std::filesystem::path path(value.data());
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path))
        throw std::runtime_error("La ruta configurada del puente Blender no existe o no es absoluta.");
    return path;
}
#endif
}
void WorldModelLibraryUI::DrawModelGrid()
{
    constexpr float size=96, cardHeight=132, gap=8;
    const float height=(std::max)(132.0f,ImGui::GetContentRegionAvail().y-36.0f);
    if(ImGui::BeginChild("archivos",{0,height},ImGuiChildFlags_Borders))
    {
        const int columns=(std::max)(1,static_cast<int>(ImGui::GetContentRegionAvail().x/(size+gap)));
        ImGuiListClipper clipper;clipper.Begin(static_cast<int>((filtered_.size()+columns-1)/columns),cardHeight);
        while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row)
        {
            for(int column=0;column<columns;++column)
            {
                const auto cell=static_cast<std::size_t>(row*columns+column);if(cell>=filtered_.size())break;
                if(column)ImGui::SameLine(0,gap);
                const auto index=filtered_[cell];const auto& asset=registry_.Models()[index];
                ImGui::PushID(static_cast<int>(index));ImGui::BeginGroup();
                if(!thumbnails_.contains(index))
                {
                    if(thumbnails_.size()>=128)thumbnails_.clear();
                    thumbnails_[index].SelectCandidate(std::filesystem::path("Data")/asset.RelativePath);
                }
                const auto origin=ImGui::GetCursorScreenPos();thumbnails_.at(index).DrawThumbnail(size);
                ImGui::SetCursorScreenPos(origin);
                if(ImGui::InvisibleButton("modelo",{size,size}))selected_=static_cast<int>(index);
                if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {selected_=static_cast<int>(index);quickPlace_=true;}
                if(selected_==static_cast<int>(index))ImGui::GetWindowDrawList()->AddRect(origin,{origin.x+size,origin.y+size},IM_COL32(70,200,235,255),0,0,2);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",asset.RelativePath.c_str());
                const auto label=asset.Name.substr(0,14);ImGui::TextUnformatted(label.c_str());
                ImGui::Dummy({size,12});ImGui::EndGroup();ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}
void WorldModelLibraryUI::DrawSelectedTexture(IWorldEditorScene& scene, const WorldModelAsset& asset)
{
    if (previewScene_ != scene.SceneIdentity() || previewPath_ != asset.RelativePath)
    { previewType_=-1; previewScene_=scene.SceneIdentity(); previewPath_=asset.RelativePath; }
    if (ImGui::Button("Vista previa con texturas"))
    {
        try { if (const auto* item=scene.LoadLibraryModel(asset.RelativePath)) previewType_=item->Type; }
        catch (const std::exception& error) { transferStatus_=error.what(); }
    }
    if (previewType_ >= 0)
    {
        const auto texture=scene.AssetThumbnail(static_cast<std::int16_t>(previewType_));
        if (texture) ImGui::Image(static_cast<ImTextureID>(texture),{192,192});
        else ImGui::TextDisabled("Preparando vista con materiales; geometria disponible abajo.");
    }
}
void WorldModelLibraryUI::Scan()
{
    attempted_=true;error_.clear();selected_=-1;folder_=0;folders_={"Todos"};
    inspection_.Clear(); thumbnails_.clear();
    try
    {
        registry_.Scan("Data");searchTexts_.clear();std::set<std::string> folders;
        for(const auto& asset:registry_.Models())
        {searchTexts_.push_back(WorldModelCatalog::Lower(asset.RelativePath));if(!WorldModelCatalog::IsGameplayEntityPath(asset.RelativePath))folders.insert(asset.Folder);}
        folders_.insert(folders_.end(),folders.begin(),folders.end());Filter();
    }
    catch(const std::exception& error){error_=error.what();filtered_.clear();}
}
void WorldModelLibraryUI::ScanCandidates()
{
    candidateAttempted_ = true;
    candidateSelected_ = -1;
    candidateInspection_.Clear();
    candidateCatalog_.Scan();
}
void WorldModelLibraryUI::ScanPackages()
{
    packagesAttempted_ = true;
    inboxSelected_ = -1;
    importedSelected_ = -1;
    importedInspection_.Clear();
    inboxPackages_ = WorldModelPackage::Discover(WorldModelPackage::ImportInbox());
    importedPackages_ = WorldModelPackage::Discover(WorldModelPackage::ImportedRoot());
}
void WorldModelLibraryUI::ScanSilverHd()
{
    if (silverHdLoading_) return;
    silverHdAttempted_ = true;
    silverHdLoading_ = true;
    silverHdSelected_ = -1;
    silverHdInspection_.Clear();
    silverHdError_.clear();

    const auto root = WorldSilverHdRegistry::EnvironmentRoot();
    const auto runtimeData = std::filesystem::absolute("Data");
    silverHdFuture_ = std::async(std::launch::async, [root, runtimeData] {
        WorldSilverHdRegistry registry;
        registry.Scan(root, runtimeData);
        return registry;
    });
}
void WorldModelLibraryUI::PollSilverHd()
{
    if (!silverHdLoading_ || !silverHdFuture_.valid()) return;
    using namespace std::chrono_literals;
    if (silverHdFuture_.wait_for(0ms) != std::future_status::ready) return;

    try
    {
        silverHdRegistry_ = silverHdFuture_.get();
    }
    catch(const std::exception& error)
    {
        silverHdError_ = error.what();
    }
    silverHdLoading_ = false;
}
void WorldModelLibraryUI::OpenInBlender(const WorldModelAsset& asset)
{
#ifdef _WIN32
    try
    {
        const auto script = BlenderBridgePath(L"SILVER_WORLD_EDITOR_BLENDER_SCRIPT");
        const auto blender = BlenderBridgePath(L"SILVER_WORLD_EDITOR_BLENDER");
        blenderProcess_.Poll();
        if (blenderProcess_.Running()) throw std::runtime_error("La sesion Blender anterior sigue abierta.");
        const auto package = WorldModelPackage::ExportOriginal("Data", asset.RelativePath);

        const auto runtime = package / "runtime";
        const auto model = runtime / std::filesystem::path(asset.RelativePath).filename();
        const auto workspace = package / "blender-work";
        // The importer creates this directory exclusively to protect previous work.
        // Creating it here makes Blender fail before it can import the model.

        std::wstring command = L"--python-exit-code 7 --python \""
            + script.wstring() + L"\" -- preview --editor --source \"" + model.wstring()
            + L"\" --textures \"" + runtime.wstring() + L"\" --output \""
            + workspace.wstring() + L"\" --import-inbox \""
            + std::filesystem::absolute(WorldModelPackage::ImportInbox()).wstring() + L"\"";
        blenderWorkspace_ = workspace; blenderImportReady_ = false;
        blenderProcess_.Start(blender, command, package / "blender-process.json");
        transferStatus_ = "Blender iniciado con copia de trabajo: " + workspace.generic_string();
    }
    catch (const std::exception& error)
    {
        transferStatus_ = std::string("Editar en Blender: ") + error.what();
    }
#else
    transferStatus_ = "Editar en Blender solo esta habilitado en Windows.";
#endif
}

void WorldModelLibraryUI::PollBlender()
{
    if (blenderWorkspace_.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - blenderLastPoll_ < std::chrono::seconds(1)) return;
    blenderLastPoll_ = now;
    try
    {
        const bool wasRunning = blenderProcess_.Running();
        blenderProcess_.Poll();
        if (!blenderImportReady_ && BlenderImportReady(blenderWorkspace_))
        {
            blenderImportReady_ = true;
            transferStatus_ = "Modelo importado en Blender. Exportar paquete LOCAL y abrir IMPORTADOS para validarlo.";
        }
        if (wasRunning && !blenderProcess_.Running())
        {
            const auto code = blenderProcess_.ExitCode().value_or(1);
            transferStatus_ = code == 0 && blenderImportReady_
                ? "Blender cerrado. Paquetes disponibles en IMPORTADOS; Data permanece intacta."
                : "Blender termino sin importacion confirmada. Codigo: " + std::to_string(code);
            ScanPackages();
        }
    }
    catch (const std::exception& error) { transferStatus_ = error.what(); }
}

const WorldPaletteItem* WorldModelLibraryUI::PlaceSelected(IWorldEditorScene& scene)
{
    if(selected_<0 || static_cast<std::size_t>(selected_)>=registry_.Models().size())return nullptr;
    const auto& asset=registry_.Models()[selected_];
    try
    {
        const auto folder="object"+std::to_string(scene.WorldNumber());
        if(WorldModelCatalog::Lower(asset.Folder)==folder)
            for(const auto& item:scene.Palette())
                if(WorldModelCatalog::Lower(item.Name)==WorldModelCatalog::Lower(asset.Name))return &item;
        const auto* item=scene.LoadLibraryModel(asset.RelativePath);
        if(!item)throw std::runtime_error("No se pudo preparar el modelo seleccionado.");
        transferStatus_.clear();return item;
    }
    catch(const std::exception& error){transferStatus_=error.what();return nullptr;}
}
void WorldModelLibraryUI::Filter()
{
    filtered_.clear();const auto text=WorldModelCatalog::Lower(search_);
    for(std::size_t i=0;i<registry_.Models().size();++i)
    {
        const auto& asset=registry_.Models()[i];
        if(WorldModelCatalog::IsGameplayEntityPath(asset.RelativePath))continue;
        if(category_!=0 && asset.Category!=Categories[category_])continue;
        if(folder_!=0 && asset.Folder!=folders_[folder_])continue;
        if(!text.empty() && searchTexts_[i].find(text)==std::string::npos)continue;
        filtered_.push_back(i);
    }
}
const WorldPaletteItem* WorldModelLibraryUI::Draw(IWorldEditorScene& scene, bool* open, bool resetLayout)
{
    if(!attempted_)Scan();
    PollSilverHd();
    PollBlender();
    const WorldPaletteItem* chosen=nullptr;
    ImGui::SetNextWindowPos({8, 148}, resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({460, 560}, resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Objetos | biblioteca de MU", open))
    {
        constexpr const char* Sources[]{"ORIGINAL MU", "SILVER REMAKE", "AI CANDIDATE", "IMPORTADOS", "SUELOS GLOBALES"};
        if(ImGui::Combo("Origen del asset", &source_, Sources, 5))
        {
            if(source_==1 && !silverHdAttempted_) ScanSilverHd();
            if(source_==2 && !candidateAttempted_) ScanCandidates();
            if(source_==3 && !packagesAttempted_) ScanPackages();
        }
        if(source_==2)
        {
            if(!candidateAttempted_)ScanCandidates();
            ImGui::TextWrapped("Candidatos leidos en modo read-only desde %s. No se copian a Data ni se agregan placements.", candidateCatalog_.Root().generic_string().c_str());
            ImGui::Text("%zu candidatos con BMD | solo se muestran paquetes no instalados", candidateCatalog_.Assets().size());
            if(ImGui::Button("Actualizar candidatos"))ScanCandidates();
            if(ImGui::BeginChild("ai-candidates", {0, 220}, ImGuiChildFlags_Borders))
            {
                for(int index=0;index<static_cast<int>(candidateCatalog_.Assets().size());++index)
                {
                    const auto& asset=candidateCatalog_.Assets()[static_cast<std::size_t>(index)];
                    const std::string label=asset.Name+" | "+std::to_string(asset.Triangles)+" tris";
                    if(ImGui::Selectable(label.c_str(), candidateSelected_==index))
                    {
                        candidateSelected_=index;
                        candidateInspection_.SelectCandidate(asset.Bmd);
                    }
                }
            }
            ImGui::EndChild();
            if(candidateSelected_>=0 && candidateSelected_<static_cast<int>(candidateCatalog_.Assets().size()))
            {
                const auto& asset=candidateCatalog_.Assets()[static_cast<std::size_t>(candidateSelected_)];
                ImGui::SeparatorText("AI CANDIDATE | preview read-only");
                ImGui::Text("Estado: %s | visual/runtime: %s", asset.Status.c_str(), asset.VisualApproval.c_str());
                ImGui::Text("BMD: %s", asset.Bmd.generic_string().c_str());
                ImGui::Text("Textura: %s", asset.Texture.empty() ? "no encontrada" : asset.Texture.generic_string().c_str());
                ImGui::Text("Tris: %zu | expected placements: %s", asset.Triangles,
                    asset.ExpectedPlacements<0 ? "no informado" : std::to_string(asset.ExpectedPlacements).c_str());
                ImGui::Text("Structural validation: %s | native-loader QA: %s",
                    asset.StructuralValidation ? "PASS" : "PENDIENTE/FAIL", asset.NativeLoaderVerified ? "PASS" : "PENDIENTE");
                ImGui::Text("LOD0: %s | LOD1: %s | LOD2: %s", asset.LOD0.c_str(), asset.LOD1.c_str(), asset.LOD2.c_str());
                candidateInspection_.Draw();
                ImGui::TextWrapped("Preview CPU del BMD aislado. Aprobar visual/runtime y presupuesto antes de cualquier futura sustitucion; esta entrega no tiene boton de aplicar.");
            }
            if(!candidateCatalog_.Error().empty())ImGui::TextWrapped("Advertencia de biblioteca: %s",candidateCatalog_.Error().c_str());
            ImGui::End();
            return nullptr;
        }
        if(source_==3)
        {
            if(!packagesAttempted_) ScanPackages();
            ImGui::SeparatorText("IMPORTADOS");
            ImGui::TextWrapped("Los paquetes importados quedan en %s. No reciben Type ni se copian a Data automaticamente.",
                WorldModelPackage::ImportedRoot().generic_string().c_str());
            ImGui::Text("%zu importados | %zu paquetes en bandeja", importedPackages_.size(), inboxPackages_.size());

            if(ImGui::Button("Actualizar importaciones")) ScanPackages();
            ImGui::SameLine();
            ImGui::TextDisabled("Bandeja: %s", WorldModelPackage::ImportInbox().generic_string().c_str());

            ImGui::SeparatorText("Bandeja de importacion");
            if(ImGui::BeginChild("package-inbox",{0,130},ImGuiChildFlags_Borders))
            {
                for(int index=0;index<static_cast<int>(inboxPackages_.size());++index)
                {
                    const auto& entry=inboxPackages_[static_cast<std::size_t>(index)];
                    const std::string label=entry.Name+" | "+std::to_string(entry.FileCount)+" archivos";
                    if(ImGui::Selectable(label.c_str(),inboxSelected_==index)) inboxSelected_=index;
                }
            }
            ImGui::EndChild();

            ImGui::BeginDisabled(inboxSelected_<0 || inboxSelected_>=static_cast<int>(inboxPackages_.size()));
            if(ImGui::Button("Importar paquete seleccionado"))
            {
                try
                {
                    const auto output=WorldModelPackage::ImportCandidate(
                        inboxPackages_[static_cast<std::size_t>(inboxSelected_)].Root);
                    transferStatus_="Importado como candidate: "+output.generic_string();
                    ScanPackages();
                }
                catch(const std::exception& error){transferStatus_=error.what();}
            }
            ImGui::EndDisabled();

            ImGui::SeparatorText("Candidates importados");
            if(ImGui::BeginChild("imported-packages",{0,180},ImGuiChildFlags_Borders))
            {
                for(int index=0;index<static_cast<int>(importedPackages_.size());++index)
                {
                    const auto& entry=importedPackages_[static_cast<std::size_t>(index)];
                    const std::string label=entry.Name+" | "+std::to_string(entry.FileCount)+" archivos";
                    if(ImGui::Selectable(label.c_str(),importedSelected_==index))
                    {
                        importedSelected_=index;
                        importedInspection_.SelectCandidate(entry.Bmd);
                    }
                }
            }
            ImGui::EndChild();

            if(importedSelected_>=0 && importedSelected_<static_cast<int>(importedPackages_.size()))
            {
                const auto& entry=importedPackages_[static_cast<std::size_t>(importedSelected_)];
                ImGui::Text("BMD: %s",entry.Bmd.generic_string().c_str());
                importedInspection_.Draw();
                ImGui::TextWrapped("Candidate importado: inspeccion habilitada. Falta registro explicito de Type/categoria runtime antes de permitir placement.");
            }
            if(!transferStatus_.empty()) ImGui::TextWrapped("%s",transferStatus_.c_str());
            ImGui::End();
            return nullptr;
        }
        if(source_==4)
        {
            ImGui::SeparatorText("SUELOS GLOBALES");
            ImGui::TextWrapped("Inventario unificado de materiales paintables encontrados en todos los WorldN. Los slots siguen exactamente el orden BITMAP_MAPTILE del cliente.");
            ImGui::Text("%zu materiales | %zu WorldN indexados", registry_.TerrainMaterials().size(), registry_.TerrainWorldCount());
            ImGui::TextDisabled("Para pintar: Terreno y zonas -> Materiales -> Biblioteca global de suelos.");

            bool changed = ImGui::InputTextWithHint("##buscar-suelo-biblioteca", "Buscar grass, ground, rock, water, ExtTile...", terrainSearch_, sizeof(terrainSearch_));
            changed = ImGui::Combo("Categoria de suelo", &terrainCategory_, TerrainCategories, 8) || changed;
            (void)changed;

            const auto search = WorldModelCatalog::Lower(terrainSearch_);
            if(ImGui::BeginChild("terrain-global-list",{0,330},ImGuiChildFlags_Borders))
            {
                int visibleIndex=0;
                for(std::size_t index=0; index<registry_.TerrainMaterials().size(); ++index)
                {
                    const auto& asset=registry_.TerrainMaterials()[index];
                    if(terrainCategory_!=0 && asset.Category!=TerrainCategories[terrainCategory_]) continue;
                    const auto searchable=WorldModelCatalog::Lower(asset.Name+" "+asset.Key+" "+asset.Category);
                    if(!search.empty() && searchable.find(search)==std::string::npos) continue;

                    const bool listed = scene.WorldNumber()>0
                        && std::binary_search(asset.Worlds.begin(),asset.Worlds.end(),scene.WorldNumber());
                    const auto runtimeName=scene.TerrainMaterialRuntimeName(asset.Slot);
                    const bool compatible=listed && scene.TerrainMaterialMatches(asset.Slot,asset.Identity);
                    const auto thumbnail=compatible ? scene.TerrainMaterialThumbnail(asset.Slot) : 0;

                    ImGui::PushID(static_cast<int>(index));
                    if(thumbnail!=0)
                    {
                        ImGui::Image(static_cast<ImTextureID>(thumbnail),{52,52});
                        ImGui::SameLine();
                    }
                    const std::string label=asset.Name+" | slot "+std::to_string(asset.Slot)
                        +" | "+std::to_string(asset.Worlds.size())+" WorldN"
                        +(compatible ? " | ACTIVO AQUI" : "");
                    if(ImGui::Selectable(label.c_str(),terrainSelected_==static_cast<int>(index),ImGuiSelectableFlags_None,{0,52}))
                        terrainSelected_=static_cast<int>(index);
                    ImGui::PopID();
                    ++visibleIndex;
                }
                if(visibleIndex==0) ImGui::TextDisabled("No hay suelos que coincidan con el filtro.");
            }
            ImGui::EndChild();

            if(terrainSelected_>=0 && terrainSelected_<static_cast<int>(registry_.TerrainMaterials().size()))
            {
                const auto& asset=registry_.TerrainMaterials()[static_cast<std::size_t>(terrainSelected_)];
                ImGui::SeparatorText("Detalle de material");
                ImGui::Text("%s | %s | slot %d",asset.Name.c_str(),asset.Category.c_str(),asset.Slot);
                ImGui::Text("%zu ocurrencias en %zu WorldN",asset.Occurrences,asset.Worlds.size());
                std::string worlds;
                for(const int world:asset.Worlds)
                {
                    if(!worlds.empty()) worlds+=", ";
                    worlds+="World"+std::to_string(world);
                    if(worlds.size()>220){worlds+="...";break;}
                }
                ImGui::TextWrapped("%s",worlds.c_str());
                const auto runtime=scene.TerrainMaterialRuntimeName(asset.Slot);
                if(!runtime.empty()) ImGui::Text("Slot actual: %s",runtime.c_str());
            }
            ImGui::End();
            return nullptr;
        }
        if(source_==1)
        {
            if(!silverHdAttempted_) ScanSilverHd();
            PollSilverHd();
            ImGui::SeparatorText("SILVER REMAKE / HD | read-only");
            if(silverHdLoading_)
            {
                ImGui::TextWrapped("Indexando Silver HD en segundo plano. La interfaz sigue disponible mientras se calculan clasificacion, deduplicacion SHA y validacion contra runtime.");
                ImGui::TextDisabled("No se modifica el worktree grafico ni Data.");
                ImGui::End();
                return nullptr;
            }
            if(!silverHdError_.empty())
            {
                ImGui::TextWrapped("No se pudo abrir el registry Silver HD: %s",silverHdError_.c_str());
                if(ImGui::Button("Reintentar Silver HD"))ScanSilverHd();
                ImGui::End();
                return nullptr;
            }

            ImGui::TextWrapped("Registry externo read-only. No copia BMD, no asigna Type y no modifica el worktree grafico.");
            ImGui::TextWrapped("Raiz: %s",PathUtf8(silverHdRegistry_.Root()).c_str());
            ImGui::Text("%zu BMD | %zu ORIGINAL | %zu REMAKE | %zu LOD | %zu CANDIDATE | %zu ROLLBACK | %zu SIN CLASIFICAR",
                silverHdRegistry_.Assets().size(),
                silverHdRegistry_.Count(WorldSilverHdState::Original),
                silverHdRegistry_.Count(WorldSilverHdState::Remake),
                silverHdRegistry_.Count(WorldSilverHdState::Lod),
                silverHdRegistry_.Count(WorldSilverHdState::Candidate),
                silverHdRegistry_.Count(WorldSilverHdState::Rollback),
                silverHdRegistry_.Count(WorldSilverHdState::Unclassified));
            ImGui::Text("%zu payloads SHA unicos | runtime: %zu EXACTO | %zu VARIANTE | %zu AUSENTE | %zu NO VALIDADO",
                silverHdRegistry_.UniquePayloadCount(),
                silverHdRegistry_.CountRuntime(WorldSilverHdRuntimeState::Exact),
                silverHdRegistry_.CountRuntime(WorldSilverHdRuntimeState::Variant),
                silverHdRegistry_.CountRuntime(WorldSilverHdRuntimeState::Missing),
                silverHdRegistry_.CountRuntime(WorldSilverHdRuntimeState::Unchecked));
            if(silverHdRegistry_.HashFailureCount()>0)
                ImGui::TextWrapped("Advertencia: %zu archivos no pudieron validarse por SHA.",silverHdRegistry_.HashFailureCount());

            bool changed=ImGui::InputTextWithHint("##buscar-silver-hd","Buscar House, Tree, LOD, candidate, ruta...",silverHdSearch_,sizeof(silverHdSearch_));
            changed=ImGui::Combo("Estado Silver HD",&silverHdState_,SilverHdStates,7)||changed;
            (void)changed;
            ImGui::SameLine();
            if(ImGui::Button("Actualizar Silver HD"))ScanSilverHd();

            const auto search=WorldModelCatalog::Lower(silverHdSearch_);
            std::vector<std::size_t> visible;
            visible.reserve(silverHdRegistry_.Assets().size());
            for(std::size_t index=0;index<silverHdRegistry_.Assets().size();++index)
            {
                const auto& asset=silverHdRegistry_.Assets()[index];
                if(WorldModelCatalog::IsGameplayEntityPath(asset.RelativePath)||WorldModelCatalog::IsGameplayEntityPath(asset.RuntimeRelativePath))continue;
                if(silverHdState_!=0 && std::string(WorldSilverHdRegistry::StateName(asset.State))!=SilverHdStates[silverHdState_])continue;
                const auto searchable=WorldModelCatalog::Lower(asset.RelativePath+" "+asset.Name+" "+asset.LogicalKey+" "+asset.Lod);
                if(!search.empty() && searchable.find(search)==std::string::npos)continue;
                visible.push_back(index);
            }

            ImGui::Text("%zu resultados",visible.size());
            if(ImGui::BeginChild("silver-hd-assets",{0,260},ImGuiChildFlags_Borders))
            {
                ImGuiListClipper clipper;clipper.Begin(static_cast<int>(visible.size()));
                while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row)
                {
                    const auto index=visible[static_cast<std::size_t>(row)];
                    const auto& asset=silverHdRegistry_.Assets()[index];
                    std::string label="[";
                    label+=WorldSilverHdRegistry::StateName(asset.State);
                    label+="] "+asset.RelativePath;
                    if(asset.VariantCount>1)label+=" | "+std::to_string(asset.VariantCount)+" variantes";
                    if(asset.DuplicateCount>1)label+=" | SHA x"+std::to_string(asset.DuplicateCount);
                    label+=" | runtime "+std::string(WorldSilverHdRegistry::RuntimeStateName(asset.RuntimeState));
                    if(!asset.Lod.empty())label+=" | "+asset.Lod;
                    if(ImGui::Selectable(label.c_str(),silverHdSelected_==static_cast<int>(index)))
                    {
                        silverHdSelected_=static_cast<int>(index);
                        silverHdInspection_.SelectCandidate(asset.Bmd);
                    }
                }
            }
            ImGui::EndChild();

            if(silverHdSelected_>=0 && silverHdSelected_<static_cast<int>(silverHdRegistry_.Assets().size()))
            {
                const auto& asset=silverHdRegistry_.Assets()[static_cast<std::size_t>(silverHdSelected_)];
                ImGui::SeparatorText("Detalle Silver HD");
                ImGui::Text("Estado: %s%s%s",WorldSilverHdRegistry::StateName(asset.State),
                    asset.Lod.empty()?"":" | ",asset.Lod.empty()?"":asset.Lod.c_str());
                ImGui::Text("Grupo logico: %s | %zu variantes",asset.LogicalKey.c_str(),asset.VariantCount);
                ImGui::Text("Runtime: %s",WorldSilverHdRegistry::RuntimeStateName(asset.RuntimeState));
                if(!asset.RuntimeRelativePath.empty())ImGui::TextWrapped("Runtime BMD: %s",asset.RuntimeRelativePath.c_str());
                ImGui::TextWrapped("BMD: %s",asset.RelativePath.c_str());
                ImGui::Text("%zu bytes | duplicados SHA: %zu",static_cast<std::size_t>(asset.Bytes),asset.DuplicateCount);
                ImGui::TextWrapped("SHA-256: %s",asset.Sha256.empty()?"NO VALIDADO":asset.Sha256.c_str());
                silverHdInspection_.Draw();
                const bool promotable=asset.State==WorldSilverHdState::Remake||asset.State==WorldSilverHdState::Candidate||asset.State==WorldSilverHdState::Lod;
                ImGui::BeginDisabled(!promotable||asset.Sha256.empty());
                if(ImGui::Button("Preparar promocion Candidate"))
                {
                    try
                    {
                        silverHdPromotion_=WorldSilverHdPromotion::Prepare(silverHdRegistry_,asset);
                        if(!WorldSilverHdPromotion::VerifySource(silverHdRegistry_,*silverHdPromotion_))throw std::runtime_error("La fuente Silver HD cambio durante la preparacion.");
                        const auto bytes=silverHdPromotion_->Encode();const auto decoded=WorldSilverHdPromotionPlan::Decode(bytes);
                        const auto output=std::filesystem::path("EditorCandidates")/"SilverHD"/(decoded.LogicalKey+".promotion.json");
                        std::filesystem::create_directories(output.parent_path());
                        std::ofstream stream(output,std::ios::binary|std::ios::trunc);if(!stream)throw std::runtime_error("No se pudo crear plan Candidate Silver HD.");
                        stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!stream)throw std::runtime_error("Plan Candidate Silver HD incompleto.");
                        silverHdPromotionStatus_="Plan Candidate preparado: "+output.generic_string();
                    }
                    catch(const std::exception& error){silverHdPromotion_.reset();silverHdPromotionStatus_=error.what();}
                }
                ImGui::EndDisabled();
                if(silverHdPromotion_)
                {
                    ImGui::Text("Promocion: %s | %zu -> %zu bytes",silverHdPromotion_->RequiresRuntimeReplacement?"REEMPLAZO PROPUESTO":"YA EXACTO",static_cast<std::size_t>(silverHdPromotion_->RuntimeBytes),static_cast<std::size_t>(silverHdPromotion_->SourceBytes));
                    ImGui::TextWrapped("Rollback baseline SHA: %s",silverHdPromotion_->RuntimeSha256.empty()?"NO DISPONIBLE (asset ausente)":silverHdPromotion_->RuntimeSha256.c_str());
                }
                if(!silverHdPromotionStatus_.empty())ImGui::TextWrapped("%s",silverHdPromotionStatus_.c_str());
                ImGui::TextWrapped("R17 no permite aplicar ni asignar Type desde Silver HD. Preparar promocion solo genera un plan Candidate con SHA/runtime baseline; no copia BMD y no modifica Data.");
            }

            ImGui::End();
            return nullptr;
        }
        ImGui::TextWrapped("Objetos de todos los mapas, items y equipamiento. Elegi una miniatura y pulsa Colocar; despues clic sobre el suelo. Esc cancela. Los items son decorativos.");
        ImGui::Text("%zu BMD originales | %zu carpetas ObjectN | %zu suelos globales | %zu WorldN | %zu importados",
            registry_.Models().size(), registry_.ModelFolderCount(), registry_.TerrainMaterials().size(),
            registry_.TerrainWorldCount(), registry_.ImportedModels().size());
        ImGui::Text("%zu resultados del filtro actual",filtered_.size());
        bool changed=ImGui::InputTextWithHint("##buscar-global","Buscar Tree, House, Object3...",search_,sizeof(search_));
        changed=ImGui::Combo("Categoria",&category_,Categories,static_cast<int>(std::size(Categories)))||changed;
        if(ImGui::BeginCombo("Carpeta de objetos",folders_[folder_].c_str()))
        {
            for(int i=0;i<static_cast<int>(folders_.size());++i)
                if(ImGui::Selectable(folders_[i].c_str(),i==folder_)){folder_=i;changed=true;}
            ImGui::EndCombo();
        }
        if(changed)Filter();
        if(ImGui::Button("Actualizar biblioteca"))Scan();
        ImGui::SameLine();
        ImGui::BeginDisabled(selected_<0);
        if(ImGui::Button("Colocar seleccionado")) chosen=PlaceSelected(scene);
        ImGui::EndDisabled();
        ImGui::TextWrapped("Selecciona una tarjeta y coloca con el boton o doble clic. NPC y monstruos: pestana NPC / Mobs.");
        if(!transferStatus_.empty())ImGui::TextWrapped("%s",transferStatus_.c_str());
        if(selected_>=0)ImGui::TextWrapped("Seleccion: %s",registry_.Models()[selected_].RelativePath.c_str());
        DrawModelGrid();
        if(quickPlace_) { quickPlace_=false; chosen=PlaceSelected(scene); }
        if(selected_>=0 && static_cast<std::size_t>(selected_)<registry_.Models().size())
        {
            const auto& asset=registry_.Models()[selected_];
            ImGui::TextWrapped("%s | %zu bytes",asset.RelativePath.c_str(),static_cast<std::size_t>(asset.Bytes));
            if(ImGui::CollapsingHeader("Vista previa y herramientas del modelo"))
            {
            DrawSelectedTexture(scene, asset);
            inspection_.SelectCandidate(std::filesystem::path("Data") / asset.RelativePath);
            inspection_.Draw();

            if(ImGui::Button("Exportar paquete"))
            {
                try
                {
                    const auto output=WorldModelPackage::ExportOriginal("Data",asset.RelativePath);
                    transferStatus_="Paquete exportado: "+output.generic_string();
                }
                catch(const std::exception& error){transferStatus_=error.what();}
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(blenderProcess_.Running());
            if(ImGui::Button("Editar en Blender")) OpenInBlender(asset);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("Salida: %s",WorldModelPackage::ExportRoot().generic_string().c_str());
            ImGui::TextWrapped("Blender trabaja sobre una copia exportada. Guardar o cerrar Blender no reemplaza Data; la vuelta al runtime requiere importar y validar el candidate.");

            }
            const WorldPaletteItem* compatible=nullptr;
            const std::string activeFolder = scene.WorldNumber() > 0
                ? "object" + std::to_string(scene.WorldNumber()) : std::string{};
            if(!activeFolder.empty() && WorldModelCatalog::Lower(asset.Folder)==activeFolder)
                for(const auto& item:scene.Palette())
                    if(WorldModelCatalog::Lower(item.Name)==WorldModelCatalog::Lower(asset.Name))
                    {compatible=&item;break;}
            if(ImGui::Button("Colocar en el mapa"))
            {
                try { chosen = compatible ? compatible : scene.LoadLibraryModel(asset.RelativePath); transferStatus_.clear(); }
                catch(const std::exception& error) { transferStatus_=error.what(); }
            }
            ImGui::TextWrapped("Clic en el mapa confirma. Esc cancela; Ctrl+Z deshace. Los recursos de otra carpeta se guardan como decoracion de biblioteca.");
        }
        if(!transferStatus_.empty())ImGui::TextWrapped("%s",transferStatus_.c_str());
        ImGui::TextWrapped("%s",error_.c_str());
    }
    ImGui::End();
    return chosen;
}
}
