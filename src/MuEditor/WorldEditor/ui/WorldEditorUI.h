#pragma once
#include "IWorldEditorScene.h"
#include "../core/WorldWorkspaceLayout.h"
#include "WorldPlacementUI.h"
#include "WorldCadUI.h"
#include "../core/WorldSelectionContext.h"
#include "../core/WorldLayerDocument.h"
#include "../core/WorldPrefabCodec.h"
#include "../core/WorldPrefabInstanceDocument.h"
#include <array>
#include <optional>
#include <string>

namespace Silver::WorldEditor
{
class WorldEditorUI
{
public:
    bool Draw(WorldObjectDocument& document, IWorldEditorScene& scene);
    void SetStatus(std::string text) { status_ = std::move(text); }
    void Reset();
    WorldWorkspaceLayout& WorkspaceLayout() { return workspace_; }
    void OpenCad() { cadOpen_ = true; }
    void SetOperation(int operation) { operation_ = operation; }
    void CancelPlacement() { placementUi_.Cancel(); startPlacement_ = false; }
    bool Placing() const { return placementUi_.Active() || startPlacement_; }
    ObjectId Selected() const { return selection_.Active(); }
    const std::vector<ObjectId>& SelectedItems() const { return selection_.Items(); }
    void SelectOnly(ObjectId id) { selection_.SelectOnly(id); }
    void ChoosePrototype(const WorldPaletteItem& item, const WorldObjectDocument& document);
    const WorldLayerDocument& Layers() const { return layers_; }
    bool LayersDirty() const { return layers_.Dirty(); }
    void MarkLayersSaved() { layers_.MarkSaved(); }
    void RestoreLayers(WorldLayerDocument layers);
    std::uint64_t LayerHistorySerial() const { return layers_.HistorySerial(); }
    std::string LayerUndoLabel() const { return layers_.UndoLabel(); }
    bool UndoLayers() { return layers_.Undo(); }
    bool RedoLayers() { return layers_.Redo(); }
private:
    WorldWorkspaceLayout workspace_;
    void DrawScenePanel(WorldObjectDocument& document, IWorldEditorScene& scene);
    void DrawInspectorPanel(WorldObjectDocument& document, IWorldEditorScene& scene, bool& save);
    void HandleObjectHotkeys(WorldObjectDocument& document);
    void DrawCommands(WorldObjectDocument& document, bool& save);
    void DrawViewportTools(WorldObjectDocument& document, IWorldEditorScene& scene);
    void DrawHistory(WorldObjectDocument& document);
    void DrawCameraBookmarks(IWorldEditorScene& scene);
    void HandleCameraBookmarkHotkeys(IWorldEditorScene& scene);
    void DrawLayers(WorldObjectDocument& document, IWorldEditorScene& scene);
    void ApplyLayerSceneState(const WorldObjectDocument& document, IWorldEditorScene& scene);
    bool ObjectEditable(ObjectId id) const;
    bool SelectionEditable() const;
    void SelectSameType(const WorldObjectDocument& document);
    void SelectSameLayer(const WorldObjectDocument& document);
    void DuplicateSelection(WorldObjectDocument& document);
    void RemoveSelection(WorldObjectDocument& document);
    void DrawPrefabs(WorldObjectDocument& document, IWorldEditorScene& scene);
    void RefreshPrefabs();
    void SaveSelectionAsPrefab(const WorldObjectDocument& document);
    void InstantiatePrefab(WorldObjectDocument& document, const WorldPrefab& prefab);
    void DrawMapNavigator(WorldObjectDocument& document, IWorldEditorScene& scene);
    void DrawObjectNavigator(WorldObjectDocument& document, IWorldEditorScene& scene);
    void DrawPalette(WorldObjectDocument& document, IWorldEditorScene& scene);
    void DrawInspector(WorldObjectDocument& document);
    void DrawMultiInspector(WorldObjectDocument& document);
    bool FocusSelected(IWorldEditorScene& scene);
    void DrawGizmo(WorldObjectDocument& document, IWorldEditorScene& scene);
    void DrawSelection(IWorldEditorScene& scene);
    void HandleBoxSelection(const WorldObjectDocument& document, IWorldEditorScene& scene);
    void HandleWorldClick(WorldObjectDocument& document, IWorldEditorScene& scene);
    void FinishWidget(WorldObjectDocument& document, const WorldObjectRecord& changed, bool edited);
    WorldSelectionContext selection_;
    WorldCadUI cadUi_;
    bool cadOpen_ = false;
    WorldLayerDocument layers_;
    WorldPrefabInstanceDocument prefabInstances_;
    WorldLayerId activeLayer_ = DefaultWorldLayer;
    bool filterToActiveLayer_ = false;
    std::uint64_t appliedLayerRevision_ = 0;
    std::uint64_t appliedLayerDocumentRevision_ = 0;
    std::optional<WorldObjectRecord> paletteModel_;
    WorldPlacementUI placementUi_;
    bool startPlacement_ = false;
    int operation_ = 0;
    int category_ = 0;
    int objectCategory_ = 0;
    bool snap_ = true;
    bool gizmoLocal_ = false;
    bool gizmoEditing_ = false;
    bool boxSelecting_ = false;
    std::array<float, 2> boxStart_{};
    bool cancelGizmoUntilRelease_ = false;
    float step_ = 50.0f;
    float rotationStep_ = 15.0f;
    float scaleStep_ = 0.1f;
    std::array<float, 3> batchPositionDelta_{};
    std::array<float, 3> batchAngleDelta_{};
    float batchScaleMultiplier_ = 1.0f;
    char search_[96]{};
    char objectSearch_[96]{};
    char newLayerName_[64]{};
    char renameLayerName_[64]{};
    char prefabName_[64]{};
    std::vector<WorldPrefab> prefabs_;
    bool prefabsLoaded_ = false;
    std::array<std::optional<WorldCameraBookmark>, 9> cameraBookmarks_{};
    std::string status_;
};
}
