#pragma once
#ifdef _EDITOR
#include "../ui/IWorldEditorScene.h"
#include "MuPlacementPreview.h"
#include "MuLibraryResources.h"
#include <unordered_map>
#include <unordered_set>
class OBJECT;

namespace Silver::WorldEditor
{
class MuWorldScene final : public IWorldEditorScene, public WorldPlacementSurface
{
public:
    void Attach(const WorldObjectDocument& document);
    void Invalidate();
    bool Attached() const { return attached_; }
    WorldEditorCamera Camera() const override;
    const WorldPlacementSurface* PlacementSurface() const override { return this; }
    std::uint64_t SceneIdentity() const override { return attached_ ? generation_ : 0; }
    int MapId() const override { return attached_ ? worldId_ : -1; }
    int WorldNumber() const override { return attached_ ? worldNumber_ : -1; }
    std::uintptr_t TerrainMaterialThumbnail(int slot) override;
    bool TerrainMaterialMatches(int slot, const std::string& path) const override;
    std::string TerrainMaterialRuntimeName(int slot) const override;
    bool ModelReady(std::int16_t type) const override;
    bool HeightAt(float x, float y, float& height) const override;
    ObjectId Pick(float mouseX, float mouseY) override;
    bool Placement(float mouseX, float mouseY, std::array<float, 3>& position) override;
    bool Bounds(ObjectId id, std::array<std::array<float, 3>, 8>& corners) const override;
    bool Focus(ObjectId id) override;
    bool FocusMapPoint(float x, float y) override;
    bool CameraMapPosition(std::array<float, 3>& position) const override;
    bool CaptureCameraBookmark(WorldCameraBookmark& bookmark) const override;
    bool RestoreCameraBookmark(const WorldCameraBookmark& bookmark) override;
    bool ZoomCamera(float steps) override;
    bool SetEditorView(int view) override;
    int EditorView() const override { return editorView_; }
    const std::vector<WorldPaletteItem>& Palette() const override { return palette_; }
    std::uintptr_t AssetThumbnail(std::int16_t type) override;
    const WorldPaletteItem* LoadLibraryModel(const std::string& path) override;
    std::string LibraryModelPath(int type) const override;
    std::uintptr_t LibraryTexturePreview(const std::string& path) override;
    int UseLibraryTerrain(const std::string& path, const TerrainMappingFile& mapping) override;
    WorldObjectFile ExportNativeObjects(const WorldObjectDocument& document) const;
    std::vector<std::uint8_t> ExportLibrary(const WorldObjectDocument& document, const std::string& baseline, const TerrainMappingFile* mapping = nullptr) const;
    void RestoreLibrary(std::span<const std::uint8_t> bytes, WorldObjectDocument& document, const std::string& baseline);
    void Synchronize(const WorldObjectDocument& document) override;
    void SetObjectEditorVisible(ObjectId id, bool visible) override;
    void SetObjectEditorSelectable(ObjectId id, bool selectable) override;
    bool MinimumModelZ(const WorldObjectRecord& record, float& minimum) const override;
    void SetPlacementPreview(const std::optional<WorldObjectRecord>& record) override { previewRecord_ = record; previewRecords_.clear(); }
    void SetPlacementPreviews(std::span<const WorldObjectRecord> records) override { previewRecord_.reset(); previewRecords_.assign(records.begin(),records.end()); }
    bool DrawPlacementPreview();
    bool DrawLibraryObjects();
    bool PreviewRuntimeEffect(int kind, int type, int subType, const std::array<float,3>& position, float scale) override;
private:
    struct Binding
    {
        OBJECT* Object = nullptr;
        int Block = -1;
        std::optional<WorldObjectRecord> Last;
    };
    bool Exists(const Binding& binding) const;
    bool QueueAssetThumbnail(std::int16_t type);
    void Apply(Binding& binding, const std::optional<WorldObjectRecord>& record);
    void Reblock(Binding& binding, int nextBlock);
    void BuildPalette(const WorldObjectDocument& document);
    void Ray(float x, float y, std::array<float, 3>& origin, std::array<float, 3>& target) const;
    ObjectId PickLibrary(const std::array<float,3>& origin, const std::array<float,3>& target) const;
    void AddLibraryPalette(int type, const std::string& path);
    MuLibraryResources library_;
    mutable MuPlacementPreview placementMesh_;
    std::optional<WorldObjectRecord> previewRecord_;
    std::vector<WorldObjectRecord> previewRecords_;
    std::uint64_t generation_ = 0;
    int worldId_ = -1;
    int worldNumber_ = -1;
    bool attached_ = false;
    int editorView_ = 0;
    std::uint64_t revision_ = 0;
    std::vector<Binding> bindings_;
    std::unordered_map<OBJECT*, ObjectId> ids_;
    std::unordered_set<ObjectId> hiddenIds_;
    std::unordered_set<ObjectId> nonSelectable_;
    std::vector<WorldPaletteItem> palette_;
    MuPlacementPreview thumbnailMesh_;
    // GPU thumbnail cache is populated lazily by the renderer. Zero/pending entries keep the safe geometry fallback.
    std::unordered_map<std::int16_t, std::uintptr_t> assetThumbnails_;
    std::unordered_map<std::int16_t, int> nextThumbnailAttemptFrame_;
};
}
#endif
