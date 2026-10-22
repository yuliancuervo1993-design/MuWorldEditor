#include "stdafx.h"
#ifdef _EDITOR
#include "MuWorldScene.h"
#include "../core/WorldCameraViews.h"
#include "MuModelThumbnail.h"
#include "../core/WorldModelCatalog.h"
#include "../core/WorldMapCatalog.h"
#include "../core/WorldObjectLinks.h"
#include "Camera/CameraState.h"
#include "Camera/CameraProjection.h"
#include "Camera/CameraManager.h"
#include "Camera/FreeFlyCamera.h"
#include "Engine/Object/ZzzObject.h"
#include "Render/Terrain/ZzzLodTerrain.h"
#include "Render/Sprites/GlobalBitmap.h"
#include "Render/Effects/ZzzEffect.h"
#include "Render/Textures/ZzzTexture.h"
#include "World/MapInfra/MapManager.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace
{
std::unordered_set<const OBJECT*> g_editorHiddenWorldObjects;
}

extern "C" bool SilverWorldEditor_ShouldRenderObject(const OBJECT* object)
{
    return object == nullptr || !g_editorHiddenWorldObjects.contains(object);
}

namespace Silver::WorldEditor
{
namespace
{
constexpr float MatchTolerance = 0.1f;
constexpr int BlockCount = 256;
// Give other palette entries the renderer's limited queue slots after a deferred GPU failure.
constexpr int ThumbnailRetryFrameDelay = 30;
constexpr float EditorFocusYaw = 225.0f;
constexpr float EditorFocusPitch = -60.0f;
constexpr float DegreesToRadians = 3.14159265358979323846f / 180.0f;

bool Matches(const OBJECT& object, const WorldObjectRecord& record)
{
    if (object.Type != record.Type || !object.Live) return false;
    for (int axis = 0; axis < 3; ++axis)
        if (std::abs(object.StartPosition[axis] - record.Position[axis]) > MatchTolerance) return false;
    return true;
}

void SnapFreeFlyToPoint(FreeFlyCamera& camera, const vec3_t center, float distance)
{
    const float yawRadians = EditorFocusYaw * DegreesToRadians;
    const float pitchRadians = EditorFocusPitch * DegreesToRadians;
    vec3_t forward{
        -std::sin(yawRadians) * std::sin(pitchRadians),
        -std::cos(yawRadians) * std::sin(pitchRadians),
        -std::cos(pitchRadians)
    };
    vec3_t position{
        center[0] - forward[0] * distance,
        center[1] - forward[1] * distance,
        center[2] - forward[2] * distance
    };
    camera.SnapToPosition(position, EditorFocusYaw, EditorFocusPitch);
}
}
void MuWorldScene::Attach(const WorldObjectDocument& document)
{
    if (gMapManager.WorldActive < 0) throw std::runtime_error("No hay un mapa cargado para editar.");
    Invalidate();
    worldId_ = gMapManager.WorldActive;
    worldNumber_ = WorldMapCatalog::PhysicalWorldNumber(worldId_);
    std::vector<Binding> bindings;
    std::unordered_map<OBJECT*, ObjectId> ids;
    bindings.reserve(document.Entries().size());
    for (const auto& entry : document.Entries())
    {
        if (!entry.Record) throw std::runtime_error("Abrir primero el baseline sin cambios.");
        if (entry.Record->BaselineOnly)
        {
            // MuMain ships a handful of sentinel/legacy records (NaN, zero or
            // negative scale). Preserve their bytes in the document but never
            // bind them to editor picking/gizmos.
            bindings.push_back({nullptr, -1, entry.Record});
            continue;
        }
        const int block = LoadedSpatialBlock(*entry.Record);
        OBJECT* match = nullptr;
        for (auto* object = ObjectBlock[block].Head; object; object = object->Next)
            if (Matches(*object, *entry.Record) && !ids.contains(object)) { match = object; break; }
        if (!match) throw std::runtime_error("El OBJ de disco no coincide con la escena. No se edito nada.");
        bindings.push_back({match, block, entry.Record}); ids.emplace(match, entry.Id);
    }
    bindings_ = std::move(bindings); ids_ = std::move(ids);
    BuildPalette(document);
    revision_ = document.Revision(); attached_ = true;
}
void MuWorldScene::Invalidate()
{
    for (const auto& binding : bindings_)
        if (binding.Object) g_editorHiddenWorldObjects.erase(binding.Object);

    library_.Clear();
    attached_ = false; worldId_ = -1; worldNumber_ = -1; bindings_.clear(); ids_.clear(); palette_.clear();
    hiddenIds_.clear(); nonSelectable_.clear();
    assetThumbnails_.clear(); nextThumbnailAttemptFrame_.clear(); thumbnailMesh_.Clear();
    mu::GetRenderer().ClearEditorThumbnails();
    ++generation_; previewRecord_.reset(); placementMesh_.Clear();
}
bool MuWorldScene::Exists(const Binding& binding) const
{
    if (!binding.Object || binding.Block < 0 || binding.Block >= BlockCount) return false;
    for (auto* object = ObjectBlock[binding.Block].Head; object; object = object->Next)
        if (object == binding.Object) return true;
    return false;
}
std::uintptr_t MuWorldScene::AssetThumbnail(std::int16_t type)
{
    if (!attached_) return 0;
    if (const auto found = assetThumbnails_.find(type); found != assetThumbnails_.end()) return found->second;

    if (const auto ready = mu::GetRenderer().FindEditorThumbnail(type); ready != 0)
    {
        assetThumbnails_.emplace(type, ready);
        return ready;
    }

    if (!ImGui::GetCurrentContext()) return 0;
    const int frame = ImGui::GetFrameCount();
    if (const auto next = nextThumbnailAttemptFrame_.find(type);
        next != nextThumbnailAttemptFrame_.end() && frame < next->second) return 0;

    // Rejected queues retry next frame; accepted work gets a bounded retry delay.
    // FindEditorThumbnail above can still return a successful result immediately.
    nextThumbnailAttemptFrame_[type] = frame + 1;
    if (QueueAssetThumbnail(type))
        nextThumbnailAttemptFrame_[type] = frame + ThumbnailRetryFrameDelay;
    return 0;
}
bool MuWorldScene::QueueAssetThumbnail(std::int16_t type)
{
    std::vector<MuPlacementPreview::PreviewMeshView> prepared;
    std::array<float, 3> low{}, high{};
    auto* mesh = library_.Mesh(type);
    if (!(mesh ? mesh : &thumbnailMesh_)->PrepareThumbnail(type, prepared, low, high)) return false;

    std::vector<mu::EditorThumbnailMesh> meshes;
    meshes.reserve(prepared.size());
    for (const auto& mesh : prepared)
        meshes.push_back({mesh.Texture, mesh.Vertices});
    return mu::GetRenderer().QueueEditorThumbnail(type, meshes, low, high);
}
std::uintptr_t MuWorldScene::TerrainMaterialThumbnail(int slot)
{
    if (!attached_ || slot < 0 || slot > 29) return 0;
    auto* bitmap = Bitmaps.FindTexture(BITMAP_MAPTILE + static_cast<GLuint>(slot));
    return bitmap && bitmap->sdlTexture
        ? reinterpret_cast<std::uintptr_t>(bitmap->sdlTexture) : 0;
}

bool MuWorldScene::TerrainMaterialMatches(int slot, const std::string& path) const
{
    if (!attached_ || slot < 0 || slot > 29) return false;
    auto* bitmap = Bitmaps.FindTexture(BITMAP_MAPTILE + static_cast<GLuint>(slot));
    if (!bitmap || !bitmap->FileName[0]) return false;
    auto actual = std::filesystem::path(bitmap->FileName).generic_string();
    auto expected = std::filesystem::path(path);
    if (expected.extension() == ".OZJ" || expected.extension() == ".ozj") expected.replace_extension(".jpg");
    if (expected.extension() == ".OZT" || expected.extension() == ".ozt") expected.replace_extension(".tga");
    actual = WorldModelCatalog::Lower(actual);
    const auto name = WorldModelCatalog::Lower(expected.generic_string());
    return actual == name || actual.ends_with("/" + name);
}
std::string MuWorldScene::TerrainMaterialRuntimeName(int slot) const
{
    if (!attached_ || slot < 0 || slot > 29) return {};
    auto* bitmap = Bitmaps.FindTexture(BITMAP_MAPTILE + static_cast<GLuint>(slot));
    if (!bitmap || !bitmap->FileName[0]) return {};
    return std::filesystem::path(bitmap->FileName).stem().string();
}

void MuWorldScene::BuildPalette(const WorldObjectDocument& document)
{
    for (int type=MODEL_WORLD_OBJECT;type<MAX_WORLD_OBJECTS;++type)
    {
        const auto& model=Models[type]; if(model.NumMeshs<=0 || !model.Meshs)continue;
        ObjectId example=NoObject;WorldObjectRecord prototype;prototype.Type=static_cast<std::int16_t>(type);
        for(const auto& entry:document.Entries())if(entry.Record && !entry.Record->BaselineOnly && entry.Record->Type==type)
        {example=entry.Id;prototype=*entry.Record;break;}
        const auto end=std::find(std::begin(model.Name),std::end(model.Name),'\0');
        std::string name(model.Name,end);if(name.empty() || name.find_first_of("/\\:")!=std::string::npos)name="Objeto "+std::to_string(type);
        palette_.push_back({static_cast<std::int16_t>(type),example,name,prototype,WorldModelCatalog::Category(name),BuildModelThumbnail(model)});
    }
    std::sort(palette_.begin(),palette_.end(),[](const auto& a,const auto& b){return std::tie(a.Category,a.Name)<std::tie(b.Category,b.Name);});
}
WorldEditorCamera MuWorldScene::Camera() const
{
    const auto size = ImGui::GetIO().DisplaySize;
    return WorldEditorCamera::FromClient(g_Camera.Matrix, g_Camera.PerspectiveX, g_Camera.PerspectiveY,
        static_cast<float>(g_Camera.ScreenCenterX), static_cast<float>(g_Camera.ScreenCenterY),
        static_cast<float>(WindowWidth), static_cast<float>(WindowHeight), g_Camera.ViewNear, g_Camera.ViewFar, size.x, size.y);
}
void MuWorldScene::Reblock(Binding& binding, int nextBlock)
{
    ReassignBlock(binding.Object, std::span<OBJECT_BLOCK>(ObjectBlock, BlockCount), binding.Block, nextBlock);
    binding.Block = nextBlock;
}
void MuWorldScene::Apply(Binding& binding, const std::optional<WorldObjectRecord>& record)
{
    if (!record) { if (binding.Object) binding.Object->Live = false; binding.Last.reset(); return; }
    if (library_.Mesh(record->Type)) { binding.Last = record; return; }
    auto position = record->Position; auto angle = record->Angle;
    if (!binding.Object)
    {
        if (std::none_of(palette_.begin(), palette_.end(), [&record](const auto& item) { return item.Type == record->Type; }))
            throw std::runtime_error("Modelo no cargado en la paleta de este mapa.");
        binding.Object = CreateObject(record->Type, position.data(), angle.data(), record->Scale);
        if (!binding.Object) throw std::runtime_error("No se pudo crear el objeto en la escena.");
        binding.Block = SpatialBlock(*record);
    }
    // Undo may restore an original negative-border object that the engine accepted.
    Reblock(binding, LoadedSpatialBlock(*record));
    auto* object = binding.Object;
    object->Live = true; object->Scale = record->Scale;
    std::copy(position.begin(), position.end(), object->Position);
    std::copy(position.begin(), position.end(), object->StartPosition);
    std::copy(angle.begin(), angle.end(), object->Angle);
    binding.Last = record;
}
void MuWorldScene::Synchronize(const WorldObjectDocument& document)
{
    if (!attached_ || gMapManager.WorldActive != worldId_) throw std::runtime_error("La escena cambio; no se usan referencias viejas.");
    if (document.Revision() == revision_) return;
    bindings_.resize(document.Entries().size());
    for (const auto& entry : document.Entries())
    {
        auto& binding = bindings_[entry.Id - 1];
        if (binding.Last == entry.Record) continue;
        if (binding.Object && !Exists(binding))
        {
            Invalidate();
            throw std::runtime_error("Objeto de escena reemplazado; volver a abrir el editor.");
        }
        Apply(binding, entry.Record);
        if (binding.Object)
        {
            ids_[binding.Object] = entry.Id;
            if (hiddenIds_.contains(entry.Id)) g_editorHiddenWorldObjects.insert(binding.Object);
            else g_editorHiddenWorldObjects.erase(binding.Object);
        }
    }
    revision_ = document.Revision();
}

void MuWorldScene::SetObjectEditorVisible(ObjectId id, bool visible)
{
    if (id == NoObject) return;
    if (visible) hiddenIds_.erase(id);
    else hiddenIds_.insert(id);

    if (!attached_ || id > bindings_.size()) return;
    auto* object = bindings_[id - 1].Object;
    if (!object) return;

    if (visible) g_editorHiddenWorldObjects.erase(object);
    else g_editorHiddenWorldObjects.insert(object);
}

void MuWorldScene::SetObjectEditorSelectable(ObjectId id, bool selectable)
{
    if (id == NoObject) return;
    if (selectable) nonSelectable_.erase(id);
    else nonSelectable_.insert(id);
}

void MuWorldScene::Ray(float x, float y, std::array<float, 3>& origin, std::array<float, 3>& target) const
{
    const auto size = ImGui::GetIO().DisplaySize;
    if (size.x <= 0 || size.y <= 0 || WindowWidth == 0 || WindowHeight == 0)
        throw std::runtime_error("Ventana no inicializada.");

    // ImGui reports logical/window coordinates. MuMain's picking path expects
    // 640x480 reference coordinates and performs the active viewport scaling
    // itself. Reuse that path so editor clicks agree exactly with game clicks.
    const float windowX = x * static_cast<float>(WindowWidth) / size.x;
    const float windowY = y * static_cast<float>(WindowHeight) / size.y;
    const int referenceX = static_cast<int>(std::lround(windowX * REFERENCE_WIDTH / static_cast<float>(WindowWidth)));
    const int referenceY = static_cast<int>(std::lround(windowY * REFERENCE_HEIGHT / static_cast<float>(WindowHeight)));

    vec3_t rayTarget{};
    CameraProjection::ScreenToWorldRay(g_Camera, referenceX, referenceY, rayTarget, true);
    std::copy_n(MousePosition, 3, origin.begin());
    std::copy_n(rayTarget, 3, target.begin());
}
ObjectId MuWorldScene::Pick(float mouseX, float mouseY)
{
    if (!attached_ || gMapManager.WorldActive != worldId_) return NoObject;
    std::array<float, 3> origin{}, target{}, oldPosition{}, oldTarget{};
    Ray(mouseX, mouseY, origin, target);
    if (const auto libraryHit = PickLibrary(origin, target); libraryHit != NoObject) return libraryHit;
    std::copy_n(MousePosition, 3, oldPosition.begin()); std::copy_n(MouseTarget, 3, oldTarget.begin());
    std::copy(origin.begin(), origin.end(), MousePosition); std::copy(target.begin(), target.end(), MouseTarget);
    auto* object = CollisionDetectObjects(nullptr); // First try exact MuMain mesh picking.
    std::copy(oldPosition.begin(), oldPosition.end(), MousePosition); std::copy(oldTarget.begin(), oldTarget.end(), MouseTarget);
    if (const auto found = ids_.find(object);
        found != ids_.end() && !nonSelectable_.contains(found->second)
        && SilverWorldEditor_ShouldRenderObject(object))
        return found->second;

    // Some legacy map objects are rendered with visibility state that makes the
    // gameplay picker skip them. Fall back to the editor bindings' OBBs so an
    // existing static OBJ instance can still be selected and transformed.
    ObjectId best = NoObject;
    float bestDistance = std::numeric_limits<float>::max();
    for (const auto& [candidate, id] : ids_)
    {
        if (!candidate || !candidate->Live || nonSelectable_.contains(id)
            || !SilverWorldEditor_ShouldRenderObject(candidate))
            continue;
        BMD* model = &Models[candidate->Type];
        model->BodyScale = candidate->Scale;
        model->CurrentAction = candidate->CurrentAction;
        VectorCopy(candidate->Position, model->BodyOrigin);
        model->Animation(BoneTransform, candidate->AnimationFrame, candidate->PriorAnimationFrame,
            candidate->PriorAction, candidate->Angle, candidate->HeadAngle, false, false);
        model->Transform(BoneTransform, candidate->BoundingBoxMin, candidate->BoundingBoxMax,
            &candidate->OBB, true);
        if (!CollisionDetectLineToOBB(origin.data(), target.data(), candidate->OBB)) continue;
        const float dx = candidate->Position[0] - origin[0];
        const float dy = candidate->Position[1] - origin[1];
        const float dz = candidate->Position[2] - origin[2];
        const float distance = dx * dx + dy * dy + dz * dz;
        if (distance < bestDistance) { bestDistance = distance; best = id; }
    }
    return best;
}
bool MuWorldScene::Placement(float mouseX, float mouseY, std::array<float, 3>& position)
{
    std::array<float, 3> origin{}, target{}, direction{};
    Ray(mouseX, mouseY, origin, target);
    float length = 0;
    for (int axis = 0; axis < 3; ++axis) { direction[axis] = target[axis]-origin[axis]; length += direction[axis]*direction[axis]; }
    length = std::sqrt(length);
    if (!std::isfinite(length) || length <= 0) return false;
    constexpr int MaximumSteps = 2048;
    constexpr float Step = 25.0f;
    for (int index = 1; index <= MaximumSteps; ++index)
    {
        const float distance = std::min(static_cast<float>(index)*Step, length);
        for (int axis = 0; axis < 3; ++axis) position[axis] = origin[axis] + direction[axis]*distance/length;
        if (position[0] >= 0 && position[1] >= 0 && position[0] < MapEdge && position[1] < MapEdge)
        {
            const float height = RequestTerrainHeight(position[0],position[1]);
            if (std::isfinite(height) && position[2] <= height) { position[2] = height; return true; }
        }
        if (distance == length) break;
    }
    return false;
}
bool MuWorldScene::Bounds(ObjectId id, std::array<std::array<float, 3>, 8>& corners) const
{
    if (!attached_ || id == NoObject || id > bindings_.size()) return false;
    const auto& binding = bindings_[id-1];
    if (binding.Last)
        if (auto* mesh = library_.Mesh(binding.Last->Type)) return mesh->Box(*binding.Last, corners);
    if (!Exists(binding) || !binding.Object->Live) return false;
    const auto& box = binding.Object->OBB;
    for (int vertex = 0; vertex < 8; ++vertex)
        for (int axis = 0; axis < 3; ++axis)
            corners[vertex][axis] = box.StartPos[axis] + ((vertex&1) ? box.XAxis[axis] : 0)
                + ((vertex&2) ? box.YAxis[axis] : 0) + ((vertex&4) ? box.ZAxis[axis] : 0);
    return true;
}

bool MuWorldScene::Focus(ObjectId id)
{
    std::array<std::array<float, 3>, 8> corners{};
    if (!Bounds(id, corners)) return false;

    auto* camera = dynamic_cast<FreeFlyCamera*>(CameraManager::Instance().GetActiveCamera());
    if (!camera) return false;

    std::array<float, 3> minimum = corners[0], maximum = corners[0];
    for (const auto& corner : corners)
        for (int axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], corner[axis]);
            maximum[axis] = std::max(maximum[axis], corner[axis]);
        }

    vec3_t center{
        (minimum[0] + maximum[0]) * 0.5f,
        (minimum[1] + maximum[1]) * 0.5f,
        (minimum[2] + maximum[2]) * 0.5f
    };
    const float dx = maximum[0] - minimum[0];
    const float dy = maximum[1] - minimum[1];
    const float dz = maximum[2] - minimum[2];
    const float radius = std::max(50.0f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
    const float distance = std::clamp(radius * 4.0f, 700.0f, 5000.0f);

    SnapFreeFlyToPoint(*camera, center, distance);
    return true;
}

bool MuWorldScene::FocusMapPoint(float x, float y)
{
    if (!attached_ || gMapManager.WorldActive != worldId_ || !std::isfinite(x) || !std::isfinite(y)
        || x < 0.0f || y < 0.0f || x >= MapEdge || y >= MapEdge)
        return false;

    auto* camera = dynamic_cast<FreeFlyCamera*>(CameraManager::Instance().GetActiveCamera());
    if (!camera) return false;

    const float height = RequestTerrainHeight(x, y);
    if (!std::isfinite(height)) return false;

    vec3_t center{x, y, height + 120.0f};
    constexpr float OverviewDistance = 2200.0f;
    SnapFreeFlyToPoint(*camera, center, OverviewDistance);
    return true;
}

bool MuWorldScene::CameraMapPosition(std::array<float, 3>& position) const
{
    if (!attached_ || gMapManager.WorldActive != worldId_
        || dynamic_cast<FreeFlyCamera*>(CameraManager::Instance().GetActiveCamera()) == nullptr)
        return false;

    std::copy_n(g_Camera.Position, 3, position.begin());
    return std::all_of(position.begin(), position.end(), [](float value) { return std::isfinite(value); });
}

bool MuWorldScene::CaptureCameraBookmark(WorldCameraBookmark& bookmark) const
{
    if (!attached_ || gMapManager.WorldActive != worldId_
        || dynamic_cast<FreeFlyCamera*>(CameraManager::Instance().GetActiveCamera()) == nullptr)
        return false;

    std::copy_n(g_Camera.Position, 3, bookmark.Position.begin());
    bookmark.Yaw = g_Camera.Angle[2];
    bookmark.Pitch = g_Camera.Angle[0];
    return std::all_of(bookmark.Position.begin(), bookmark.Position.end(),
        [](float value) { return std::isfinite(value); })
        && std::isfinite(bookmark.Yaw) && std::isfinite(bookmark.Pitch);
}

bool MuWorldScene::RestoreCameraBookmark(const WorldCameraBookmark& bookmark)
{
    if (!attached_ || gMapManager.WorldActive != worldId_) return false;
    auto* camera = dynamic_cast<FreeFlyCamera*>(CameraManager::Instance().GetActiveCamera());
    if (!camera) return false;
    if (!std::all_of(bookmark.Position.begin(), bookmark.Position.end(),
            [](float value) { return std::isfinite(value); })
        || !std::isfinite(bookmark.Yaw) || !std::isfinite(bookmark.Pitch))
        return false;

    vec3_t position;
    Vector(bookmark.Position[0], bookmark.Position[1], bookmark.Position[2], position);
    camera->SnapToPosition(position, bookmark.Yaw, bookmark.Pitch);
    return true;
}

bool MuWorldScene::PreviewRuntimeEffect(int kind, int type, int subType, const std::array<float,3>& position, float scale)
{
    if (!attached_ || gMapManager.WorldActive != worldId_ || type < 0 || !std::isfinite(scale) || scale <= 0.0f) return false;
    vec3_t pos, angle, light; Vector(position[0],position[1],position[2],pos); Vector(0.0f,0.0f,0.0f,angle); Vector(1.0f,1.0f,1.0f,light);
    if(kind==0){CreateParticle(type,pos,angle,light,subType,scale);return true;}
    if(kind==1){CreateEffect(type,pos,angle,light,subType,nullptr,-1,0,0,0,scale);return true;}
    return false;
}


bool MuWorldScene::SetEditorView(int view)
{
    if (!attached_ || gMapManager.WorldActive != worldId_ || view < 0 || view > 3) return false;
    auto* camera = dynamic_cast<FreeFlyCamera*>(CameraManager::Instance().GetActiveCamera());
    WorldCameraBookmark current{};
    if (!camera || !CaptureCameraBookmark(current)) return false;
    // Resolve the point under the viewport center, so changing view keeps the same subject.
    std::array<float, 3> target{};
    const auto viewport = Camera();
    if (!Placement(viewport.Width * 0.5f, viewport.Height * 0.5f, target))
    {
        const float x = std::clamp(current.Position[0], 0.0f, 25500.0f);
        const float y = std::clamp(current.Position[1], 0.0f, 25500.0f);
        target = {x, y, RequestTerrainHeight(x, y)};
    }
    float distanceSquared = 0.0f;
    for (std::size_t axis = 0; axis < target.size(); ++axis)
        distanceSquared += (current.Position[axis] - target[axis]) * (current.Position[axis] - target[axis]);
    const auto pose = WorldCameraViews::FromTarget(view, target, std::clamp(std::sqrt(distanceSquared), 700.0f, 18000.0f));
    if (!pose) return false;
    vec3_t position; std::copy(pose->Position.begin(), pose->Position.end(), position);
    camera->SnapToPosition(position, pose->Yaw, pose->Pitch);
    editorView_ = view;
    g_Camera.TopViewEnable = false; // The editor owns its camera; do not enable gameplay camera overrides.
    return true;
}

bool MuWorldScene::ZoomCamera(float steps)
{
    if (!attached_ || gMapManager.WorldActive != worldId_ || !std::isfinite(steps) || steps == 0.0f)
        return false;
    auto* camera = dynamic_cast<FreeFlyCamera*>(CameraManager::Instance().GetActiveCamera());
    if (!camera) return false;
    camera->Zoom(steps);
    return true;
}
}
#endif
