#pragma once
#include "../core/WorldObjectDocument.h"
#include "../core/WorldEditorCamera.h"
#include <array>
#include <string>
#include "WorldModelThumbnail.h"
#include "../core/WorldPlacementSurface.h"
#include "../core/TerrainMappingCodec.h"

namespace Silver::WorldEditor
{
struct WorldCameraBookmark
{
    std::array<float, 3> Position{};
    float Yaw = 0.0f;
    float Pitch = -90.0f;
};

struct WorldPaletteItem
{
    std::int16_t Type = 0;
    ObjectId Example = NoObject;
    std::string Name;
    WorldObjectRecord Prototype;
    std::string Category;
    std::vector<WorldModelThumbnail::Triangle> Thumbnail;
};
class IWorldEditorScene
{
public:
    virtual ~IWorldEditorScene() = default;
    virtual WorldEditorCamera Camera() const = 0;
    virtual ObjectId Pick(float mouseX, float mouseY) = 0;
    virtual bool Placement(float mouseX, float mouseY, std::array<float, 3>& position) = 0;
    virtual bool Bounds(ObjectId id, std::array<std::array<float, 3>, 8>& corners) const = 0;
    virtual bool Focus(ObjectId) { return false; }
    virtual bool FocusMapPoint(float, float) { return false; }
    virtual bool CameraMapPosition(std::array<float, 3>&) const { return false; }
    virtual bool CaptureCameraBookmark(WorldCameraBookmark&) const { return false; }
    virtual bool RestoreCameraBookmark(const WorldCameraBookmark&) { return false; }
    virtual bool ZoomCamera(float) { return false; }
    virtual bool SetEditorView(int) { return false; }
    virtual int EditorView() const { return 0; }
    virtual const std::vector<WorldPaletteItem>& Palette() const = 0;
    // Native GPU thumbnail handle (SDL_GPUTexture* on the SDL_gpu backend).
    // Zero means the preview is not ready and the UI must keep its geometry fallback.
    virtual std::uintptr_t AssetThumbnail(std::int16_t) { return 0; }
    virtual std::uintptr_t TerrainMaterialThumbnail(int) { return 0; }
    virtual bool TerrainMaterialMatches(int, const std::string&) const { return false; }
    virtual std::string TerrainMaterialRuntimeName(int) const { return {}; }
    virtual const WorldPaletteItem* LoadLibraryModel(const std::string&) { return nullptr; }
    virtual std::string LibraryModelPath(int) const { return {}; }
    virtual std::uintptr_t LibraryTexturePreview(const std::string&) { return 0; }
    virtual int UseLibraryTerrain(const std::string&, const TerrainMappingFile&) { return -1; }
    virtual void Synchronize(const WorldObjectDocument& document) = 0;
    virtual void SetObjectEditorVisible(ObjectId, bool) {}
    virtual void SetObjectEditorSelectable(ObjectId, bool) {}
    virtual const WorldPlacementSurface* PlacementSurface() const { return nullptr; }
    virtual std::uint64_t SceneIdentity() const { return 0; }
    virtual int MapId() const { return -1; }
    virtual int WorldNumber() const { return -1; }
    virtual void SetPlacementPreview(const std::optional<WorldObjectRecord>&) {}
    virtual void SetPlacementPreviews(std::span<const WorldObjectRecord>) {}
    virtual bool PreviewRuntimeEffect(int, int, int, const std::array<float,3>&, float) { return false; }
};
}
