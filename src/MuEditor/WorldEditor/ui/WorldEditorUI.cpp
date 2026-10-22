#include "WorldEditorUI.h"
#include "WorldWorkspacePanel.h"
#include "../core/WorldMapNavigator.h"
#include "imgui.h"
#include "../third_party/ImGuizmo/ImGuizmo.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "Core/Platform/ContentHash.h"
#include <stdexcept>
#include <string_view>

namespace Silver::WorldEditor
{
namespace
{
constexpr ImU32 SelectionColor = IM_COL32(255, 200, 70, 240);

bool ContainsInsensitive(std::string_view text, std::string_view query)
{
    if (query.empty()) return true;
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
        [](unsigned char left, unsigned char right) { return std::tolower(left) == std::tolower(right); }) != text.end();
}

const WorldPaletteItem* PaletteItemForType(const IWorldEditorScene& scene, std::int16_t type)
{
    const auto& palette = scene.Palette();
    const auto found = std::find_if(palette.begin(), palette.end(),
        [type](const WorldPaletteItem& item) { return item.Type == type; });
    return found == palette.end() ? nullptr : &*found;
}

bool PreviewButton(IWorldEditorScene& scene, const WorldPaletteItem& item, bool selected)
{
    const ImVec2 size{ImGui::GetContentRegionAvail().x, 100};
    const auto origin=ImGui::GetCursorScreenPos();
    ImGui::PushID(item.Type);
    const bool clicked=ImGui::InvisibleButton("modelo",size);
    auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 end{origin.x+size.x,origin.y+size.y};
    draw->AddRectFilled(origin,end,selected?IM_COL32(58,75,92,255):IM_COL32(27,34,42,255),5);
    draw->PushClipRect(origin,end,true);
    const auto gpuThumbnail = scene.AssetThumbnail(item.Type);
    if (gpuThumbnail != 0)
    {
        const ImTextureID texture = static_cast<ImTextureID>(gpuThumbnail);
        draw->AddImage(texture, {origin.x + 3.0f, origin.y + 3.0f}, {end.x - 3.0f, origin.y + 76.0f});
    }
    else for(const auto& triangle:item.Thumbnail)
    {
        ImVec2 points[3];for(int i=0;i<3;++i)points[i]={origin.x+triangle[i][0]*size.x,origin.y+triangle[i][1]*76};
        draw->AddTriangleFilled(points[0],points[1],points[2],IM_COL32(91,150,125,150));
        draw->AddTriangle(points[0],points[1],points[2],IM_COL32(180,215,193,100));
    }
    draw->AddText({origin.x+4,origin.y+80},IM_COL32(230,230,230,255),item.Name.c_str());
    draw->PopClipRect();
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s | Tipo %d\nPreview GPU con materiales cuando esta listo; geometria segura como fallback.",item.Name.c_str(),item.Type);
    ImGui::PopID();return clicked;
}
}
void WorldEditorUI::Reset()
{
    cadUi_.Reset();cadOpen_=false;
    selection_.Clear(); paletteModel_.reset(); CancelPlacement();
    layers_.Reset();
    activeLayer_ = DefaultWorldLayer;
    filterToActiveLayer_ = false;
    appliedLayerRevision_ = 0;
    appliedLayerDocumentRevision_ = 0;
    gizmoEditing_ = cancelGizmoUntilRelease_ = false; boxSelecting_ = false;
    batchPositionDelta_.fill(0.0f); batchAngleDelta_.fill(0.0f); batchScaleMultiplier_ = 1.0f;
    status_.clear(); search_[0] = 0; objectSearch_[0] = 0;
    newLayerName_[0] = 0; renameLayerName_[0] = 0; prefabName_[0] = 0;
    prefabs_.clear(); prefabsLoaded_ = false;
    cameraBookmarks_.fill(std::nullopt);
    category_ = 0; objectCategory_ = 0;
}

void WorldEditorUI::RestoreLayers(WorldLayerDocument layers)
{
    layers_ = std::move(layers);
    layers_.AcceptLoadedState();
    activeLayer_ = DefaultWorldLayer;
    filterToActiveLayer_ = false;
    appliedLayerRevision_ = 0;
    appliedLayerDocumentRevision_ = 0;
    const auto* base = layers_.Find(DefaultWorldLayer);
    std::snprintf(renameLayerName_, sizeof(renameLayerName_), "%s", base ? base->Name.c_str() : "Base");
}
void WorldEditorUI::DrawScenePanel(WorldObjectDocument& document, IWorldEditorScene& scene)
{
    if (!workspace_.Visible(WorkspacePanel::Scene)) return;
    if (Workspace::Begin("Escena | objetos y capas###WorldScene", workspace_, WorkspacePanel::Scene))
    {
        ImGui::TextDisabled("Escena activa | World%d", scene.WorldNumber());
        if (ImGui::TreeNodeEx("WORLD", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawLayers(document, scene);
            ApplyLayerSceneState(document, scene);
            DrawPrefabs(document, scene);

            ImGui::TreeNodeEx("Terrain", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            DrawMapNavigator(document, scene);
            DrawObjectNavigator(document, scene);

            ImGui::TreeNodeEx("Collisions / ATT  [terrain]", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

void WorldEditorUI::DrawInspectorPanel(WorldObjectDocument& document, IWorldEditorScene& scene, bool& save)
{
    if (!workspace_.Visible(WorkspacePanel::Inspector) || workspace_.Visible(WorkspacePanel::Details)) return;
    if (Workspace::Begin("Propiedades###WorldInspector", workspace_, WorkspacePanel::Inspector))
    {
        DrawCommands(document, save);
        DrawHistory(document);
        DrawCameraBookmarks(scene);
        ImGui::Separator();
        if (selection_.Count() == 1 && !Placing())
        {
            DrawInspector(document);
            ImGui::BeginDisabled(!ObjectEditable(selection_.Active()));
            if (ImGui::Button("Duplicar con vista previa"))
            {
                if (const auto* record = document.Find(selection_.Active()))
                { paletteModel_ = *record; startPlacement_ = true; selection_.Clear(); }
            }
            ImGui::EndDisabled();
            if (ImGui::Button("Limpiar seleccion")) selection_.Clear();
        }
        else if (selection_.Count() > 1 && !Placing())
        {
            DrawMultiInspector(document);
            if (ImGui::Button("Limpiar seleccion")) selection_.Clear();
        }
        else
        {
            ImGui::TextWrapped("Selecciona un objeto en el viewport o en Hierarchy para editar su transformacion.");
            ImGui::TextDisabled("Ctrl+clic agrega o quita objetos de la seleccion. El gizmo se habilita con una sola seleccion.");
        }
        ImGui::SeparatorText("Estado");
        ImGui::TextWrapped("%s", status_.c_str());
    }

    ImGui::End();
}

bool WorldEditorUI::Draw(WorldObjectDocument& document, IWorldEditorScene& scene)
{
    bool save = false;
    try
    {
        selection_.Prune(document);
        layers_.Prune(document);
        ApplyLayerSceneState(document, scene);
        ImGuizmo::BeginFrame();
        DrawScenePanel(document, scene);
        DrawInspectorPanel(document, scene, save);
        if (workspace_.Visible(WorkspacePanel::Assets))
        {
            if (Workspace::Begin("Modelos del mapa###WorldAssets", workspace_, WorkspacePanel::Assets))
                DrawPalette(document, scene);
            ImGui::End();
        }
        HandleObjectHotkeys(document);
        const auto& io = ImGui::GetIO();
        if (!io.WantTextInput && !document.HasPreview() && !Placing()
            && selection_.Active() != NoObject && ImGui::IsKeyPressed(ImGuiKey_F, false))
            FocusSelected(scene);
        HandleCameraBookmarkHotkeys(scene);
        DrawViewportTools(document, scene);
    }
    catch (const std::exception& error) { status_ = error.what(); save = false; }
    return save;
}
void WorldEditorUI::DrawViewportTools(WorldObjectDocument& document,IWorldEditorScene& scene)
{
    if(cadOpen_)
    {CancelPlacement();cadUi_.Draw(document,scene,selection_.Active(),cadOpen_);return;}
    if (startPlacement_ && paletteModel_)
    { placementUi_.Choose(*paletteModel_, document, scene); startPlacement_ = false; }
    DrawGizmo(document,scene);DrawSelection(scene);
    HandleBoxSelection(document,scene);HandleWorldClick(document,scene);
}

void WorldEditorUI::DrawCommands(WorldObjectDocument& document, bool& save)
{
    ImGui::BeginDisabled(document.HasPreview() || Placing());
    ImGui::TextDisabled("Undo/Redo: History global (Ctrl+Z / Ctrl+Y).");
    if (ImGui::Button("Guardar copia de prueba")) save = true;
    if (ImGui::Button("CAD / Distribuir objetos")) cadOpen_=true;
    ImGui::TextUnformatted(document.Dirty() ? "Hay cambios sin guardar" : "Copia guardada / sin cambios");
    ImGui::EndDisabled();
}

void WorldEditorUI::HandleObjectHotkeys(WorldObjectDocument& document)
{
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        document.Cancel(); CancelPlacement();
        if (gizmoEditing_) cancelGizmoUntilRelease_ = true;
        gizmoEditing_ = false;
    }
    if (!ImGui::GetIO().WantTextInput && !document.HasPreview() && !Placing())
    {
        if (ImGui::GetIO().KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateSelection(document);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) RemoveSelection(document);
    }
}
void WorldEditorUI::DrawCameraBookmarks(IWorldEditorScene& scene)
{
    if (!ImGui::CollapsingHeader("Vistas guardadas"))
        return;

    ImGui::TextDisabled("Ctrl+1..9 guarda | 1..9 recupera posicion y orientacion.");
    ImGui::SeparatorText("Vistas de trabajo");
    const char* names[]{"3D","Superior","Frontal","Lateral"};
    for(int view=0;view<4;++view)
    {
        if(view)ImGui::SameLine();
        ImGui::BeginDisabled(scene.EditorView()==view);
        if(ImGui::Button(names[view])){if(scene.SetEditorView(view))status_=std::string("Vista ")+names[view]+" activada.";else status_="No se pudo cambiar la vista.";}
        ImGui::EndDisabled();
    }
    if(scene.EditorView()!=0)ImGui::TextDisabled("Vista axial segura: orientacion fija. Proyeccion ortografica GPU sigue bloqueada hasta tener soporte world-pass.");
    for (std::size_t index = 0; index < cameraBookmarks_.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        const std::string label = std::to_string(index + 1)
            + (cameraBookmarks_[index] ? " [guardado]" : " [vacio]");
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Guardar"))
        {
            WorldCameraBookmark bookmark;
            if (scene.CaptureCameraBookmark(bookmark))
            {
                cameraBookmarks_[index] = bookmark;
                status_ = "Camera bookmark " + std::to_string(index + 1) + " guardado.";
            }
            else status_ = "No se pudo capturar la camara activa.";
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!cameraBookmarks_[index].has_value());
        if (ImGui::Button("Ir"))
        {
            if (scene.RestoreCameraBookmark(*cameraBookmarks_[index]))
                status_ = "Camera bookmark " + std::to_string(index + 1) + " restaurado.";
            else status_ = "No se pudo restaurar el camera bookmark.";
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void WorldEditorUI::HandleCameraBookmarkHotkeys(IWorldEditorScene& scene)
{
    if (ImGui::GetIO().WantTextInput) return;
    static constexpr std::array<ImGuiKey, 9> keys{
        ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5,
        ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9
    };
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        if (!ImGui::IsKeyPressed(keys[index], false)) continue;
        if (ImGui::GetIO().KeyCtrl)
        {
            WorldCameraBookmark bookmark;
            if (scene.CaptureCameraBookmark(bookmark))
            {
                cameraBookmarks_[index] = bookmark;
                status_ = "Camera bookmark " + std::to_string(index + 1) + " guardado.";
            }
        }
        else if (cameraBookmarks_[index])
        {
            if (scene.RestoreCameraBookmark(*cameraBookmarks_[index]))
                status_ = "Camera bookmark " + std::to_string(index + 1) + " restaurado.";
        }
    }
}

void WorldEditorUI::DrawHistory(WorldObjectDocument& document)
{
    if (!ImGui::CollapsingHeader("History"))
        return;

    const std::string objectUndo = document.UndoLabel();
    const std::string objectRedo = document.RedoLabel();
    const std::string layerUndo = layers_.UndoLabel();
    const std::string layerRedo = layers_.RedoLabel();

    ImGui::TextDisabled("Objetos");
    ImGui::TextWrapped("Undo: %s", objectUndo.empty() ? "(vacio)" : objectUndo.c_str());
    ImGui::TextWrapped("Redo: %s", objectRedo.empty() ? "(vacio)" : objectRedo.c_str());
    ImGui::TextDisabled("Layers");
    ImGui::TextWrapped("Undo: %s", layerUndo.empty() ? "(vacio)" : layerUndo.c_str());
    ImGui::TextWrapped("Redo: %s", layerRedo.empty() ? "(vacio)" : layerRedo.c_str());
    ImGui::TextDisabled("Los streams locales son backend; la sesion usa una linea temporal global.");
}

bool WorldEditorUI::FocusSelected(IWorldEditorScene& scene)
{
    const ObjectId selected = selection_.Active();
    if (selected == NoObject)
    {
        status_ = "Selecciona un objeto para enfocar la camara.";
        return false;
    }
    if (!layers_.IsVisible(selected))
    {
        status_ = "El objeto esta en una capa oculta. Mostrala antes de enfocarlo.";
        return false;
    }
    if (!scene.Focus(selected))
    {
        status_ = "No se pudo enfocar el objeto; verifica que siga cargado en el mapa actual.";
        return false;
    }
    status_ = "Camara enfocada en objeto #" + std::to_string(selected) + ".";
    return true;
}

bool WorldEditorUI::ObjectEditable(ObjectId id) const
{
    return id != NoObject && layers_.IsVisible(id) && !layers_.IsLocked(id);
}

bool WorldEditorUI::SelectionEditable() const
{
    return std::all_of(selection_.Items().begin(), selection_.Items().end(),
        [this](ObjectId id) { return ObjectEditable(id); });
}

void WorldEditorUI::SelectSameType(const WorldObjectDocument& document)
{
    const auto* active = document.Find(selection_.Active());
    if (!active) return;
    selection_.Clear();
    for (const auto& entry : document.Entries())
        if (entry.Record && !entry.Record->BaselineOnly && entry.Record->Type == active->Type
            && layers_.IsVisible(entry.Id) && !layers_.IsLocked(entry.Id))
            selection_.Add(entry.Id);
    status_ = "Seleccionados " + std::to_string(selection_.Count()) + " objetos del mismo modelo.";
}

void WorldEditorUI::SelectSameLayer(const WorldObjectDocument& document)
{
    const ObjectId active = selection_.Active();
    if (active == NoObject) return;
    const WorldLayerId layer = layers_.LayerFor(active);
    selection_.Clear();
    for (const auto& entry : document.Entries())
        if (entry.Record && !entry.Record->BaselineOnly && layers_.LayerFor(entry.Id) == layer
            && layers_.IsVisible(entry.Id) && !layers_.IsLocked(entry.Id))
            selection_.Add(entry.Id);
    status_ = "Seleccionados " + std::to_string(selection_.Count()) + " objetos de la misma capa.";
}

void WorldEditorUI::DuplicateSelection(WorldObjectDocument& document)
{
    if (selection_.Empty() || !SelectionEditable()) return;
    std::vector<WorldObjectRecord> records;
    records.reserve(selection_.Count());
    for (ObjectId id : selection_.Items())
    {
        const auto* source = document.Find(id);
        if (!source) continue;
        auto copy = *source;
        copy.Position[0] += step_;
        copy.Position[1] += step_;
        records.push_back(copy);
    }
    if (records.empty()) return;

    const WorldLayerId destination = layers_.LayerFor(selection_.Active());
    const auto ids = document.AddMany(records, "Duplicar seleccion");
    layers_.AssignMany(ids, destination);
    selection_.Clear();
    for (ObjectId id : ids) selection_.Add(id);
    if (const auto* active = document.Find(selection_.Active())) paletteModel_ = *active;
    status_ = "Duplicados " + std::to_string(ids.size()) + " objetos como una sola accion.";
}

void WorldEditorUI::RemoveSelection(WorldObjectDocument& document)
{
    if (selection_.Empty() || !SelectionEditable()) return;
    const auto ids = selection_.Items();
    if (document.RemoveMany(ids, "Retirar seleccion"))
    {
        selection_.Clear();
        paletteModel_.reset();
        status_ = "Retirados " + std::to_string(ids.size()) + " objetos como una sola accion.";
    }
}

void WorldEditorUI::ApplyLayerSceneState(const WorldObjectDocument& document, IWorldEditorScene& scene)
{
    if (appliedLayerRevision_ == layers_.Revision()
        && appliedLayerDocumentRevision_ == document.Revision())
        return;

    for (const auto& entry : document.Entries())
    {
        if (!entry.Record || entry.Record->BaselineOnly) continue;
        scene.SetObjectEditorVisible(entry.Id, layers_.IsVisible(entry.Id));
        scene.SetObjectEditorSelectable(entry.Id, layers_.IsVisible(entry.Id) && !layers_.IsLocked(entry.Id));
    }
    appliedLayerRevision_ = layers_.Revision();
    appliedLayerDocumentRevision_ = document.Revision();
}

void WorldEditorUI::RefreshPrefabs()
{
    prefabs_.clear();
    const std::filesystem::path root = std::filesystem::path("EditorCandidates") / "Prefabs";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) throw std::runtime_error("No se pudo preparar la biblioteca de Prefabs.");

    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (entry.is_symlink() || !entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        const auto size = entry.file_size();
        if (size == 0 || size > 512 * 1024) continue;
        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream) continue;
        std::vector<std::uint8_t> bytes{
            std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        try { prefabs_.push_back(WorldPrefabCodec::Decode(bytes)); }
        catch (const std::exception&) {}
    }
    std::sort(prefabs_.begin(), prefabs_.end(),
        [](const WorldPrefab& left, const WorldPrefab& right) { return left.Name < right.Name; });
    prefabsLoaded_ = true;
}

void WorldEditorUI::SaveSelectionAsPrefab(const WorldObjectDocument& document)
{
    if (selection_.Empty()) throw std::invalid_argument("Selecciona uno o mas objetos para crear un Prefab.");

    std::vector<WorldObjectRecord> records;
    records.reserve(selection_.Count());
    std::array<float, 3> pivot{};
    for (ObjectId id : selection_.Items())
    {
        const auto* record = document.Find(id);
        if (!record || record->BaselineOnly)
            throw std::invalid_argument("La seleccion contiene un objeto no exportable.");
        records.push_back(*record);
        for (std::size_t axis = 0; axis < 3; ++axis) pivot[axis] += record->Position[axis];
    }
    for (float& value : pivot) value /= static_cast<float>(records.size());

    const std::string name = prefabName_;
    const auto prefab = WorldPrefabCodec::FromSelection(name, records, pivot);
    const auto bytes = WorldPrefabCodec::Encode(prefab);
    const std::filesystem::path root = std::filesystem::path("EditorCandidates") / "Prefabs";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) throw std::runtime_error("No se pudo crear la carpeta de Prefabs.");

    std::string safe;
    for (unsigned char value : name)
        safe.push_back(std::isalnum(value) || value == '-' || value == '_' ? static_cast<char>(value) : '_');
    if (safe.empty()) throw std::invalid_argument("Nombre de Prefab invalido.");

    const auto target = root / (safe + ".json");
    if (std::filesystem::is_symlink(target))
        throw std::runtime_error("Destino de Prefab inseguro.");
    std::ofstream stream(target, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("No se pudo guardar el Prefab.");
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("No se pudo completar el Prefab.");

    prefabName_[0] = 0;
    RefreshPrefabs();
    status_ = "Prefab guardado en Candidate/Prefabs. Data original intacto.";
}

void WorldEditorUI::InstantiatePrefab(WorldObjectDocument& document, const WorldPrefab& prefab)
{
    std::array<float, 3> anchor{};
    if (const auto* active = document.Find(selection_.Active()))
        anchor = active->Position;
    else
        throw std::invalid_argument("Selecciona un objeto del mapa como punto de anclaje del Prefab.");

    const auto records = WorldPrefabCodec::Instantiate(prefab, anchor);
    const auto ids = document.AddMany(records, "Instanciar Prefab " + prefab.Name);
    const WorldLayerId destination = selection_.Active() != NoObject
        ? layers_.LayerFor(selection_.Active()) : DefaultWorldLayer;
    layers_.AssignMany(ids, destination);
    const auto prefabBytes=WorldPrefabCodec::Encode(prefab); const auto prefabSha=mu::crypto::Sha256Hex(std::string_view(reinterpret_cast<const char*>(prefabBytes.data()),prefabBytes.size())); prefabInstances_.Add(prefab.Name,prefabSha,ids);
    selection_.Clear();
    for (ObjectId id : ids) selection_.Add(id);
    if (const auto* active = document.Find(selection_.Active())) paletteModel_ = *active;
    status_ = "Prefab " + prefab.Name + " instanciado: " + std::to_string(ids.size())
        + " objetos. Undo lo revierte como grupo.";
}

void WorldEditorUI::DrawPrefabs(WorldObjectDocument& document, IWorldEditorScene& scene)
{
    if (!prefabsLoaded_)
    {
        try { RefreshPrefabs(); }
        catch (const std::exception& error) { status_ = error.what(); prefabsLoaded_ = true; }
    }
    if (!ImGui::CollapsingHeader("Prefabs", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::InputTextWithHint("##prefab_name", "Nombre del Prefab...", prefabName_, sizeof(prefabName_));
    ImGui::SameLine();
    ImGui::BeginDisabled(selection_.Empty() || prefabName_[0] == 0);
    if (ImGui::Button("Guardar seleccion"))
    {
        try { SaveSelectionAsPrefab(document); }
        catch (const std::exception& error) { status_ = error.what(); }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Recargar")) RefreshPrefabs();

    ImGui::TextDisabled("Instanciar usa el objeto seleccionado como anclaje; nunca escribe Data original.");
    for (const auto& prefab : prefabs_)
    {
        ImGui::PushID(prefab.Name.c_str());
        ImGui::Text("%s | %zu objetos", prefab.Name.c_str(), prefab.Records.size());
        ImGui::SameLine();
        ImGui::BeginDisabled(selection_.Empty());
        if(ImGui::Button("Preview"))
        {
            if(const auto* anchor=document.Find(selection_.Active())){const auto records=WorldPrefabCodec::Instantiate(prefab,anchor->Position);scene.SetPlacementPreviews(records);status_="Ghost preview Prefab: "+prefab.Name+" | "+std::to_string(records.size())+" objetos. No persistido.";}
        }
        ImGui::SameLine();
        if (ImGui::Button("Instanciar"))
        {
            try { InstantiatePrefab(document, prefab); }
            catch (const std::exception& error) { status_ = error.what(); }
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void WorldEditorUI::DrawLayers(WorldObjectDocument& document, IWorldEditorScene& scene)
{
    if (!ImGui::CollapsingHeader("Capas", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::TextDisabled("V = visible | L = bloqueada");
    ImGui::PushItemWidth(190.0f);
    ImGui::InputTextWithHint("##nueva_capa", "Nueva capa...", newLayerName_, sizeof(newLayerName_));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("+ Crear"))
    {
        try
        {
            activeLayer_ = layers_.Add(newLayerName_);
            const auto* layer = layers_.Find(activeLayer_);
            std::snprintf(renameLayerName_, sizeof(renameLayerName_), "%s", layer ? layer->Name.c_str() : "");
            newLayerName_[0] = 0;
            status_ = "Capa creada.";
        }
        catch (const std::exception& error) { status_ = error.what(); }
    }

    for (const auto& current : layers_.Layers())
    {
        ImGui::PushID(static_cast<int>(current.Id));
        bool visible = current.Visible;
        bool locked = current.Locked;
        if (ImGui::Checkbox("V", &visible))
        {
            layers_.SetVisible(current.Id, visible);
            if (!visible && current.Id == activeLayer_)
                status_ = "Capa oculta en viewport y picking.";
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("L", &locked))
        {
            layers_.SetLocked(current.Id, locked);
            if (locked && current.Id == activeLayer_)
                status_ = "Capa bloqueada: permanece visible pero no se puede editar.";
        }
        ImGui::SameLine();

        std::size_t count = 0;
        for (const auto& entry : document.Entries())
            if (entry.Record && !entry.Record->BaselineOnly && layers_.LayerFor(entry.Id) == current.Id)
                ++count;

        const std::string label = current.Name + " (" + std::to_string(count) + ")";
        if (ImGui::Selectable(label.c_str(), current.Id == activeLayer_))
        {
            activeLayer_ = current.Id;
            std::snprintf(renameLayerName_, sizeof(renameLayerName_), "%s", current.Name.c_str());
        }
        ImGui::PopID();
    }

    const auto* active = layers_.Find(activeLayer_);
    if (!active)
    {
        activeLayer_ = DefaultWorldLayer;
        active = layers_.Find(activeLayer_);
    }
    if (!active) return;

    if (renameLayerName_[0] == 0)
        std::snprintf(renameLayerName_, sizeof(renameLayerName_), "%s", active->Name.c_str());

    ImGui::PushItemWidth(190.0f);
    ImGui::InputText("Nombre capa activa", renameLayerName_, sizeof(renameLayerName_));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Renombrar"))
    {
        try
        {
            if (layers_.Rename(activeLayer_, renameLayerName_))
                status_ = "Capa renombrada.";
        }
        catch (const std::exception& error) { status_ = error.what(); }
    }

    ImGui::BeginDisabled(selection_.Empty());
    if (ImGui::Button("Asignar seleccion a capa"))
    {
        try
        {
            if (layers_.AssignMany(selection_.Items(), activeLayer_))
                status_ = std::to_string(selection_.Count()) + " objeto(s) asignados a " + active->Name + ".";
            else
                status_ = "La seleccion ya pertenece a esa capa.";
        }
        catch (const std::exception& error) { status_ = error.what(); }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Filtrar Outliner", &filterToActiveLayer_);

    ImGui::BeginDisabled(activeLayer_ == DefaultWorldLayer);
    if (ImGui::Button("Eliminar capa"))
    {
        try
        {
            if (layers_.Remove(activeLayer_))
            {
                activeLayer_ = DefaultWorldLayer;
                const auto* base = layers_.Find(activeLayer_);
                std::snprintf(renameLayerName_, sizeof(renameLayerName_), "%s", base ? base->Name.c_str() : "Base");
                status_ = "Capa eliminada; sus objetos volvieron a Base.";
            }
        }
        catch (const std::exception& error) { status_ = error.what(); }
    }
    ImGui::EndDisabled();

    ImGui::TextDisabled("Layers participa del History global. Ctrl+Z/Ctrl+Y respeta el orden cronologico entre dominios.");
    ApplyLayerSceneState(document, scene);
}

void WorldEditorUI::DrawMapNavigator(WorldObjectDocument& document, IWorldEditorScene& scene)
{
    const std::string mapHeader = scene.WorldNumber() > 0
        ? "Mapa 2D | World" + std::to_string(scene.WorldNumber()) : "Mapa 2D";
    if (!ImGui::CollapsingHeader(mapHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::TextWrapped("Clic en el plano: mueve solo la camara del editor. Los puntos son objetos del OBJ del mapa actual.");
    const float side = std::min(260.0f, ImGui::GetContentRegionAvail().x);
    if (side < 96.0f)
    {
        ImGui::TextUnformatted("Panel demasiado angosto para mostrar el mapa.");
        return;
    }

    const bool blocked = document.HasPreview() || Placing() || gizmoEditing_;
    ImGui::BeginDisabled(blocked);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##mapa_lorencia", {side, side});
    const bool hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end{origin.x + side, origin.y + side};
    draw->AddRectFilled(origin, end, IM_COL32(18, 23, 29, 255), 3.0f);
    draw->AddRect(origin, end, IM_COL32(105, 120, 132, 255), 3.0f);

    for (int line = 1; line < 16; ++line)
    {
        const float offset = side * static_cast<float>(line) / 16.0f;
        draw->AddLine({origin.x + offset, origin.y}, {origin.x + offset, end.y}, IM_COL32(65, 74, 82, 95));
        draw->AddLine({origin.x, origin.y + offset}, {end.x, origin.y + offset}, IM_COL32(65, 74, 82, 95));
    }

    const auto toCanvas = [origin, side](float x, float y, ImVec2& point)
    {
        std::array<float, 2> normalized{};
        if (!WorldMapNavigator::WorldToNormalized(x, y, normalized)) return false;
        point = {origin.x + normalized[0] * side, origin.y + normalized[1] * side};
        return true;
    };

    for (const auto& entry : document.Entries())
    {
        if (!entry.Record || entry.Record->BaselineOnly || !layers_.IsVisible(entry.Id)) continue;
        const auto& record = *entry.Record;
        if (record.Position[0] < 0.0f || record.Position[1] < 0.0f
            || record.Position[0] >= MapEdge || record.Position[1] >= MapEdge)
            continue;
        ImVec2 point{};
        if (!toCanvas(record.Position[0], record.Position[1], point)) continue;
        const bool active = entry.Id == selection_.Active();
        const bool selected = selection_.Contains(entry.Id);
        draw->AddCircleFilled(point, active ? 3.2f : (selected ? 2.2f : 1.25f),
            active ? SelectionColor : (selected ? IM_COL32(255, 200, 70, 150) : IM_COL32(156, 166, 174, 165)));
    }

    std::array<float, 3> cameraPosition{};
    if (scene.CameraMapPosition(cameraPosition))
    {
        ImVec2 camera{};
        if (toCanvas(cameraPosition[0], cameraPosition[1], camera))
        {
            draw->AddCircleFilled(camera, 4.0f, IM_COL32(80, 210, 255, 255));
            draw->AddCircle(camera, 6.0f, IM_COL32(210, 245, 255, 230), 0, 1.5f);
        }
    }

    if (hovered)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float normalizedX = std::clamp((mouse.x - origin.x) / side, 0.0f, 1.0f);
        const float normalizedY = std::clamp((mouse.y - origin.y) / side, 0.0f, 1.0f);
        std::array<float, 2> world{};
        if (WorldMapNavigator::NormalizedToWorld(normalizedX, normalizedY, world))
        {
            ImVec2 cross{};
            if (toCanvas(world[0], world[1], cross))
            {
                draw->AddLine({cross.x - 5.0f, cross.y}, {cross.x + 5.0f, cross.y}, IM_COL32(255, 255, 255, 190));
                draw->AddLine({cross.x, cross.y - 5.0f}, {cross.x, cross.y + 5.0f}, IM_COL32(255, 255, 255, 190));
            }

            ImGui::SetTooltip("X %.0f | Y %.0f\nCelda %d, %d",
                world[0], world[1], static_cast<int>(world[0] / 100.0f), static_cast<int>(world[1] / 100.0f));

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (scene.FocusMapPoint(world[0], world[1]))
                    status_ = "Camara movida a celda " + std::to_string(static_cast<int>(world[0] / 100.0f))
                        + "," + std::to_string(static_cast<int>(world[1] / 100.0f)) + ".";
                else
                    status_ = "No se pudo mover la camara a esa zona del mapa.";
            }
        }
    }
    ImGui::EndDisabled();

    if (blocked)
        ImGui::TextDisabled("Termina o cancela la accion actual para usar el mapa.");
    else
        ImGui::TextDisabled("Arriba: Y 255 | abajo: Y 0 | izquierda: X 0 | derecha: X 255");
}

void WorldEditorUI::DrawObjectNavigator(WorldObjectDocument& document, IWorldEditorScene& scene)
{
    if (!ImGui::CollapsingHeader("Objetos colocados", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    constexpr const char* categories[]{"Todos","Arboles","Vegetacion","Edificios","Muros y puertas","Rocas y terreno","Muebles","Puentes y caminos","Luces y decoracion","Agua y efectos","Ruinas y estructuras","Sin clasificar"};
    ImGui::InputTextWithHint("##buscar_objetos", "Buscar #ID, Type o nombre...", objectSearch_, sizeof(objectSearch_));
    ImGui::Combo("Categoria de objetos", &objectCategory_, categories, 12);
    ImGui::TextWrapped("Clic selecciona uno; Ctrl+clic agrega o quita. Doble clic o F centra la camara en el objeto activo.");

    std::vector<ObjectId> visible;
    visible.reserve(document.Entries().size());
    std::size_t active = 0, preserved = 0;
    const std::string_view query{objectSearch_};
    for (const auto& entry : document.Entries())
    {
        if (!entry.Record) continue;
        if (entry.Record->BaselineOnly) { ++preserved; continue; }
        ++active;
        if (filterToActiveLayer_ && layers_.LayerFor(entry.Id) != activeLayer_) continue;
        const auto* palette = PaletteItemForType(scene, entry.Record->Type);
        if (objectCategory_ != 0 && (!palette || palette->Category != categories[objectCategory_])) continue;
        const std::string name = palette ? palette->Name : ("Objeto " + std::to_string(entry.Record->Type));
        const auto* layer = layers_.Find(layers_.LayerFor(entry.Id));
        const std::string label = "#" + std::to_string(entry.Id) + " | " + name
            + " | " + (palette ? palette->Category : "Sin clasificar")
            + " | Type " + std::to_string(entry.Record->Type)
            + " | Capa " + (layer ? layer->Name : "Base");
        if (ContainsInsensitive(label, query))
            visible.push_back(entry.Id);
    }

    ImGui::Text("%zu visibles / %zu editables | %zu seleccionados", visible.size(), active, selection_.Count());
    if (preserved != 0)
        ImGui::TextDisabled("%zu registros legacy preservados sin editar", preserved);
    ImGui::BeginDisabled(document.HasPreview() || Placing());
    if (ImGui::BeginChild("objetos_colocados", {0, 170}, ImGuiChildFlags_Borders))
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const ObjectId id = visible[static_cast<std::size_t>(index)];
                const auto* record = document.Find(id);
                if (!record) continue;
                const auto* palette = PaletteItemForType(scene, record->Type);
                const auto* layer = layers_.Find(layers_.LayerFor(id));
                const std::string name = palette ? palette->Name : ("Objeto " + std::to_string(record->Type));
                const std::string state = !layers_.IsVisible(id) ? " [oculto]"
                    : (layers_.IsLocked(id) ? " [bloqueado]" : "");
                const std::string label = "#" + std::to_string(id) + " | " + name
                    + " | T" + std::to_string(record->Type)
                    + " | " + (layer ? layer->Name : "Base") + state;

                const bool locked = layers_.IsLocked(id);
                ImGui::BeginDisabled(locked);
                if (ImGui::Selectable(label.c_str(), selection_.Contains(id)))
                {
                    CancelPlacement();
                    if (ImGui::GetIO().KeyCtrl) selection_.Toggle(id);
                    else if (ImGui::GetIO().KeyShift) selection_.Add(id);
                    else selection_.SelectOnly(id);
                    if (const auto* activeRecord = document.Find(selection_.Active())) paletteModel_ = *activeRecord;
                    else paletteModel_.reset();
                }
                const bool doubleClicked = ImGui::IsItemHovered()
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                ImGui::EndDisabled();
                if (doubleClicked && layers_.IsVisible(id) && !locked)
                {
                    selection_.SelectOnly(id);
                    paletteModel_ = *record;
                    FocusSelected(scene);
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(selection_.Active() == NoObject || document.Find(selection_.Active()) == nullptr);
    if (ImGui::Button("Enfocar activo [F]"))
        FocusSelected(scene);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}

void WorldEditorUI::ChoosePrototype(const WorldPaletteItem& item, const WorldObjectDocument& document)
{
    if (document.HasPreview()) return;
    cadOpen_ = false;
    cadUi_.Reset();
    selection_.Clear(); paletteModel_ = item.Prototype; startPlacement_ = true;
    status_ = "Vista fantasma activa: move el cursor al suelo, ajusta giro/tamano y hace clic para colocar.";
}
void WorldEditorUI::DrawPalette(WorldObjectDocument& document, IWorldEditorScene& scene)
{
    constexpr const char* categories[]{"Todos","Arboles","Vegetacion","Edificios","Muros y puertas","Rocas y terreno","Muebles","Puentes y caminos","Luces y decoracion","Agua y efectos","Ruinas y estructuras","Sin clasificar"};
    ImGui::InputTextWithHint("##buscar", "Buscar modelo...", search_, sizeof(search_));
    ImGui::Combo("Categoria",&category_,categories,12);
    std::vector<const WorldPaletteItem*> visible;
    visible.reserve(scene.Palette().size());
    const std::string_view query{search_};
    for (const auto& item : scene.Palette())
    {
        if (category_ != 0 && item.Category != categories[category_]) continue;
        if (!ContainsInsensitive(item.Name, query)) continue;
        visible.push_back(&item);
    }
    ImGui::Text("%zu visibles / %zu modelos cargados en este mapa", visible.size(), scene.Palette().size());
    ImGui::TextDisabled("Este Asset Browser es local al mapa. Usa 'Biblioteca MU' para ver el inventario global de ObjectN e importados.");
    ImGui::TextWrapped("Filtra por categoria o nombre. Un clic en una miniatura activa el fantasma 3D; al mover el mouse al mapa, la copia sigue el suelo hasta que confirmes.");
    ImGui::BeginDisabled(document.HasPreview());
    const float paletteHeight = std::max(96.0f, ImGui::GetContentRegionAvail().y - 72.0f);
    if (ImGui::BeginChild("modelos", {0, paletteHeight}, ImGuiChildFlags_Borders))
    {
        constexpr float AssetCardWidth = 190.0f;
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / AssetCardWidth));
        if (ImGui::BeginTable("paleta", columns))
        {
            ImGuiListClipper clipper;
            const int rows = static_cast<int>((visible.size() + static_cast<std::size_t>(columns) - 1)
                / static_cast<std::size_t>(columns));
            clipper.Begin(rows);
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    for (int column = 0; column < columns; ++column)
                    {
                        const auto index = static_cast<std::size_t>(row * columns + column);
                        if (index >= visible.size()) break;
                        ImGui::TableNextColumn();
                        const auto& item = *visible[index];
                        if (PreviewButton(scene, item, paletteModel_ && paletteModel_->Type == item.Type))
                            ChoosePrototype(item, document);
                    }
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::BeginDisabled(!paletteModel_);
    if (ImGui::Button("Colocar otra copia")) startPlacement_ = true;
    ImGui::EndDisabled();
    placementUi_.Controls();
    ImGui::EndDisabled();
}
void WorldEditorUI::FinishWidget(WorldObjectDocument& document, const WorldObjectRecord& changed, bool edited)
{
    const ObjectId selected = selection_.Active();
    if (selected == NoObject) return;
    if (ImGui::IsItemActivated() && !document.HasPreview()) document.Begin(selected, "Transformar objeto");
    if (edited)
    {
        try
        {
            if (!document.HasPreview()) document.Begin(selected, "Transformar objeto");
            document.Preview(changed);
        }
        catch (const std::exception& error) { status_ = error.what(); }
    }
    if (ImGui::IsItemDeactivated() && document.HasPreview() && !gizmoEditing_) document.Commit();
}
void WorldEditorUI::DrawInspector(WorldObjectDocument& document)
{
    const ObjectId selected = selection_.Active();
    const auto* found = document.Find(selected);
    if (!found) return;
    auto record = *found;
    if (ImGui::CollapsingHeader("Objeto colocado", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto* layer = layers_.Find(layers_.LayerFor(selected));
        const bool editable = ObjectEditable(selected);
        ImGui::Text("Modelo %d | Objeto %u", record.Type, selected);
        ImGui::Text("Capa: %s", layer ? layer->Name.c_str() : "Base");
        if (!layers_.IsVisible(selected))
            ImGui::TextDisabled("La capa esta oculta. Volve a mostrarla para editar este objeto.");
        else if (layers_.IsLocked(selected))
            ImGui::TextDisabled("La capa esta bloqueada. Desbloqueala para editar este objeto.");
        ImGui::TextWrapped("Edita posicion, giro o tamano. Las flechas 3D modifican el objeto en la copia de trabajo; Deshacer revierte la ultima accion.");
        ImGui::BeginDisabled(gizmoEditing_ || !editable);
        const bool positionChanged = ImGui::DragFloat3("Posicion", record.Position.data(), 5.0f);
        FinishWidget(document, record, positionChanged);
        if (const auto* current = document.Find(selected)) record = *current;
        const bool angleChanged = ImGui::DragFloat3("Giro", record.Angle.data(), 1.0f);
        FinishWidget(document, record, angleChanged);
        if (const auto* current = document.Find(selected)) record = *current;
        const bool scaleChanged = ImGui::DragFloat("Tamano uniforme", &record.Scale, 0.01f, MinimumScale, MaximumScale);
        FinishWidget(document, record, scaleChanged);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(document.HasPreview() || !editable);
        ImGui::SeparatorText("Gizmo 3D");
        ImGui::RadioButton("Mover", &operation_, 0); ImGui::SameLine();
        ImGui::RadioButton("Girar", &operation_, 1); ImGui::SameLine();
        ImGui::RadioButton("Escalar", &operation_, 2);
        ImGui::Checkbox("Ajustar a pasos", &snap_);
        ImGui::SameLine();
        if (ImGui::RadioButton("Global", !gizmoLocal_)) gizmoLocal_ = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("Local", gizmoLocal_)) gizmoLocal_ = true;
        ImGui::DragFloat("Snap movimiento", &step_, 1.0f, 1.0f, 500.0f);
        ImGui::DragFloat("Snap giro", &rotationStep_, 1.0f, 1.0f, 180.0f);
        ImGui::DragFloat("Snap escala", &scaleStep_, 0.01f, 0.01f, 2.0f, "%.2f");
        if (ImGui::Button("Retirar objeto")) ImGui::OpenPopup("Confirmar retiro");
        if (ImGui::BeginPopup("Confirmar retiro"))
        {
            ImGui::TextUnformatted("Solo de la copia. Se puede deshacer.");
            if (ImGui::Button("Confirmar"))
            {
                document.Remove(selected);
                selection_.Remove(selected);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
    }
}
void WorldEditorUI::DrawMultiInspector(WorldObjectDocument& document)
{
    ImGui::SeparatorText("Seleccion multiple");
    ImGui::Text("%zu objetos seleccionados", selection_.Count());
    const bool editable = SelectionEditable();
    if (!editable)
        ImGui::TextDisabled("La seleccion contiene objetos ocultos o bloqueados. Mostra/desbloquea sus capas para transformar el grupo.");
    ImGui::TextWrapped("Los valores son relativos y se aplican al grupo como una sola accion de Undo/Redo. El gizmo 3D queda reservado para seleccion individual.");
    ImGui::BeginDisabled(!editable);
    ImGui::DragFloat3("Mover grupo", batchPositionDelta_.data(), 5.0f);
    ImGui::DragFloat3("Girar grupo", batchAngleDelta_.data(), 1.0f);
    ImGui::DragFloat("Escala multiplicador", &batchScaleMultiplier_, 0.01f, 0.01f, 10.0f, "%.3f");

    ImGui::BeginDisabled(document.HasPreview());
    if (ImGui::Button("Aplicar al grupo"))
    {
        try
        {
            if (!std::isfinite(batchScaleMultiplier_) || batchScaleMultiplier_ <= 0.0f)
                throw std::invalid_argument("El multiplicador de escala debe ser finito y mayor que cero.");

            std::vector<WorldObjectUpdate> updates;
            updates.reserve(selection_.Count());
            for (const ObjectId id : selection_.Items())
            {
                const auto* source = document.Find(id);
                if (!source) continue;
                auto changed = *source;
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    changed.Position[axis] += batchPositionDelta_[axis];
                    changed.Angle[axis] += batchAngleDelta_[axis];
                }
                changed.Scale *= batchScaleMultiplier_;
                updates.push_back({id, changed});
            }

            if (document.SetMany(updates, "Transformar seleccion multiple"))
            {
                status_ = "Transformacion aplicada a " + std::to_string(updates.size()) + " objetos como una sola accion.";
                batchPositionDelta_.fill(0.0f);
                batchAngleDelta_.fill(0.0f);
                batchScaleMultiplier_ = 1.0f;
            }
            else status_ = "La transformacion grupal no produjo cambios.";
        }
        catch (const std::exception& error) { status_ = error.what(); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Restablecer valores"))
    {
        batchPositionDelta_.fill(0.0f);
        batchAngleDelta_.fill(0.0f);
        batchScaleMultiplier_ = 1.0f;
    }
    ImGui::EndDisabled();

    if (ImGui::Button("Duplicar grupo [Ctrl+D]")) DuplicateSelection(document);
    ImGui::SameLine();
    if (ImGui::Button("Retirar grupo [Del]")) RemoveSelection(document);
    if (ImGui::Button("Seleccionar mismo modelo")) SelectSameType(document);
    ImGui::SameLine();
    if (ImGui::Button("Seleccionar misma capa")) SelectSameLayer(document);

    ImGui::EndDisabled();
    ImGui::TextDisabled("Ctrl+clic alterna | Shift+clic agrega | Ctrl+D duplica | Del retira | Ctrl+Z/Y historial.");
}

void WorldEditorUI::DrawGizmo(WorldObjectDocument& document, IWorldEditorScene& scene)
{
    if (selection_.Count() != 1 || Placing()) return;
    const ObjectId selectedId = selection_.Active();
    const auto* selected = document.Find(selectedId);
    if (!selected || !ObjectEditable(selectedId)) return;
    if (cancelGizmoUntilRelease_)
    {
        ImGuizmo::Enable(false);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) cancelGizmoUntilRelease_ = false;
        return;
    }
    ImGuizmo::Enable(true);
    auto record = *selected;
    const auto camera = scene.Camera();
    float model[16], scale[3]{record.Scale, record.Scale, record.Scale};
    ImGuizmo::RecomposeMatrixFromComponents(record.Position.data(), record.Angle.data(), scale, model);
    const auto operation = operation_ == 0 ? ImGuizmo::TRANSLATE : (operation_ == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE);
    const float value = operation_ == 0 ? step_ : (operation_ == 1 ? rotationStep_ : scaleStep_);
    const float snap[3]{value, value, value};
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0, 0, camera.Width, camera.Height);
    ImGuizmo::SetOrthographic(false);
    const bool changed = ImGuizmo::Manipulate(camera.View.data(), camera.Projection.data(), operation,
        gizmoLocal_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD, model, nullptr, snap_ ? snap : nullptr);
    if (ImGuizmo::IsUsing() && !gizmoEditing_)
    {
        if (document.HasPreview()) return;
        document.Begin(selectedId, "Transformacion con flechas"); gizmoEditing_ = true;
    }
    if (changed && gizmoEditing_)
    {
        std::array<float, 3> position{}, angle{}, size{};
        ImGuizmo::DecomposeMatrixToComponents(model, position.data(), angle.data(), size.data());
        if (operation_ == 0) record.Position = position;
        if (operation_ == 1) record.Angle = angle;
        if (operation_ == 2) record.Scale = *std::max_element(size.begin(), size.end(), [record](float a, float b) { return std::abs(a-record.Scale) < std::abs(b-record.Scale); });
        try { document.Preview(record); }
        catch (const std::exception& error) { status_ = error.what(); }
    }
    if (gizmoEditing_ && !ImGuizmo::IsUsing()) { document.Commit(); gizmoEditing_ = false; }
}
void WorldEditorUI::DrawSelection(IWorldEditorScene& scene)
{
    if (selection_.Empty()) return;
    const auto camera = scene.Camera();
    constexpr int edges[12][2]{{0,1},{0,2},{0,4},{1,3},{1,5},{2,3},{2,6},{3,7},{4,5},{4,6},{5,7},{6,7}};
    auto* draw = ImGui::GetBackgroundDrawList();
    for (const ObjectId id : selection_.Items())
    {
        if (!layers_.IsVisible(id)) continue;
        std::array<std::array<float, 3>, 8> corners{};
        if (!scene.Bounds(id, corners)) continue;
        const bool active = id == selection_.Active();
        const ImU32 color = active ? SelectionColor : IM_COL32(255, 200, 70, 145);
        const float thickness = active ? 2.0f : 1.25f;
        for (const auto& edge : edges)
        {
            std::array<float, 2> first{}, second{};
            if (camera.Project(corners[edge[0]], first) && camera.Project(corners[edge[1]], second))
                draw->AddLine({first[0],first[1]}, {second[0],second[1]}, color, thickness);
        }
    }
}
void WorldEditorUI::HandleBoxSelection(const WorldObjectDocument& document, IWorldEditorScene& scene)
{
    const auto& io = ImGui::GetIO();
    const bool blocked = document.HasPreview() || Placing() || ImGuizmo::IsOver() || ImGuizmo::IsUsing()
        || io.WantTextInput || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

    if (!boxSelecting_ && !blocked && io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const auto mouse = ImGui::GetMousePos();
        boxStart_ = {mouse.x, mouse.y};
        boxSelecting_ = true;
    }
    if (!boxSelecting_) return;

    const auto mouse = ImGui::GetMousePos();
    const ImVec2 start{boxStart_[0], boxStart_[1]};
    const ImVec2 current{mouse.x, mouse.y};
    const ImVec2 min{std::min(start.x, current.x), std::min(start.y, current.y)};
    const ImVec2 max{std::max(start.x, current.x), std::max(start.y, current.y)};
    auto* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(min, max, IM_COL32(70, 150, 235, 28));
    draw->AddRect(min, max, IM_COL32(105, 185, 255, 230), 0.0f, 0, 1.5f);

    if (io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        boxSelecting_ = false;
        return;
    }
    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) return;

    boxSelecting_ = false;
    const float width = max.x - min.x, height = max.y - min.y;
    if (width < 5.0f && height < 5.0f)
    {
        const ObjectId picked = scene.Pick(current.x, current.y);
        if (picked != NoObject && ObjectEditable(picked)) selection_.Add(picked);
        if (const auto* record = document.Find(selection_.Active())) paletteModel_ = *record;
        return;
    }

    const auto camera = scene.Camera();
    selection_.Clear();
    for (const auto& entry : document.Entries())
    {
        if (!entry.Record || entry.Record->BaselineOnly || !ObjectEditable(entry.Id)) continue;
        std::array<float, 2> screen{};
        if (!camera.Project(entry.Record->Position, screen)) continue;
        if (screen[0] >= min.x && screen[0] <= max.x && screen[1] >= min.y && screen[1] <= max.y)
            selection_.Add(entry.Id);
    }
    if (const auto* record = document.Find(selection_.Active())) paletteModel_ = *record;
    else paletteModel_.reset();
    status_ = "Seleccion rectangular: " + std::to_string(selection_.Count()) + " objeto(s).";
}

void WorldEditorUI::HandleWorldClick(WorldObjectDocument& document, IWorldEditorScene& scene)
{
    ObjectId placementSelected = selection_.Active();
    if (placementUi_.Update(document, scene, placementSelected))
    {
        if (placementSelected != selection_.Active())
        {
            const auto* layer = layers_.Find(activeLayer_);
            const WorldLayerId destination = layer && layer->Visible && !layer->Locked
                ? activeLayer_ : DefaultWorldLayer;
            layers_.Assign(placementSelected, destination);
            selection_.SelectOnly(placementSelected);
            if (const auto* record = document.Find(placementSelected)) paletteModel_ = *record;
        }
        return;
    }
    if (boxSelecting_ || document.HasPreview() || ImGuizmo::IsOver() || ImGuizmo::IsUsing()
        || ImGui::GetIO().WantTextInput || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;

    const auto mouse = ImGui::GetMousePos();
    const ObjectId picked = scene.Pick(mouse.x, mouse.y);
    if (ImGui::GetIO().KeyCtrl)
    {
        if (picked != NoObject) selection_.Toggle(picked);
    }
    else if (ImGui::GetIO().KeyShift)
    {
        if (picked != NoObject) selection_.Add(picked);
    }
    else selection_.SelectOnly(picked);

    if (const auto* record = document.Find(selection_.Active())) paletteModel_ = *record;
    else paletteModel_.reset();
}
}
