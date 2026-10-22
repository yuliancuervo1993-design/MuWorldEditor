#include "stdafx.h"
#ifdef _EDITOR
#include "WorldEditorIntegration.h"
#include "../../Core/MuEditorCore.h"
#include "MuWorldScene.h"
#include "MuTerrainScene.h"
#include "../core/WorldTerrainFiles.h"
#include "../core/WorldMapCatalog.h"
#include "../ui/WorldTerrainUI.h"
#include "../ui/WorldModelLibraryUI.h"
#include "../ui/WorldNpcLibraryUI.h"
#include "../ui/WorldEffectsUI.h"
#include "../core/WorldEffectDocument.h"
#include "../core/WorldRuntimeEffectCatalog.h"
#include "../core/WorldRuntimeEffectCatalog.h"
#include "../core/WorldObjectCandidate.h"
#include "../core/WorldCandidateManifest.h"
#include "../core/WorldCandidateVerifier.h"
#include "../core/WorldCandidateBrowser.h"
#include "../core/WorldCandidateCompare.h"
#include "../core/WorldLayerSidecar.h"
#include "../core/WorldHistoryCoordinator.h"
#include "../core/WorldMapValidator.h"
#include "../core/WorldMonsterSpawnDocument.h"
#include "../core/WorldOpenMuBackupManifest.h"
#include "../core/WorldOpenMuApplyPlan.h"
#include "../core/WorldOpenMuDryRun.h"
#include "../core/WorldRegionDocument.h"
#include "../core/WorldTriggerDocument.h"
#include "../core/WorldGateDocument.h"
#include "../core/WorldGateCandidateFile.h"
#include "../ui/WorldGateUI.h"
#include "../core/SilverWorldStudioBridge.h"
#include "../core/WorldAiCommandBatch.h"
#include "../core/WorldAiGameplayBatch.h"
#include "../core/WorldAiSpawnBatch.h"
#include "../core/WorldAiEffectBatch.h"
#include "../core/WorldAiTerrainBatch.h"
#include "../core/WorldEditorPolicy.h"
#include "Scenes/SceneCore.h"
#include "Scenes/SceneCommon.h"
#include "../ui/WorldEditorUI.h"
#include "../ui/WorldWorkspacePanel.h"
#include "../ui/WorldTriggerFlowUI.h"
#include "../ui/WorldAiAssistantUI.h"
#include "World/MapInfra/MapManager.h"
#include "Engine/Object/ZzzInterface.h"
#include "Render/Terrain/ZzzLodTerrain.h"
#include "Render/Renderer/MuRenderer.h"
#include "Camera/CameraManager.h"
#include "Camera/CameraMode.h"
#include "Core/Platform/WinNls.h"
#include "Core/Platform/ContentHash.h"
#include "Data/GameConfig/GameConfig.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

extern unsigned int WindowWidth;
extern unsigned int WindowHeight;
extern BOOL g_bUseWindowMode;
extern BOOL g_bUseFullscreenMode;
extern bool Destroy;
extern CErrorReport g_ErrorReport;
void MuApplyWindowResolution(unsigned int width, unsigned int height, bool windowed);
float RequestTerrainHeight(float xf, float yf);

namespace Silver::WorldEditor
{
namespace
{
struct EditorWindowPreset
{
    unsigned int Width;
    unsigned int Height;
    const char* Label;
};

constexpr EditorWindowPreset WindowPresets[]{
    {1024, 768, "1024 x 768"},
    {1280, 720, "1280 x 720"},
    {1280, 800, "1280 x 800"},
    {1366, 768, "1366 x 768"},
    {1440, 900, "1440 x 900"},
    {1600, 900, "1600 x 900"},
    {1680, 1050, "1680 x 1050"},
    {1920, 1080, "1920 x 1080"},
    {2560, 1440, "2560 x 1440"}
};

int FindWindowPreset(unsigned int width, unsigned int height)
{
    for (int index = 0; index < static_cast<int>(std::size(WindowPresets)); ++index)
        if (WindowPresets[index].Width == width && WindowPresets[index].Height == height)
            return index;
    return -1;
}

constexpr const char* EnabledVariable = "SILVER_WORLD_EDITOR_LAB";
constexpr const char* StandaloneVariable = "SILVER_WORLD_EDITOR_STANDALONE";
constexpr const char* MarkerName = "silver-world-editor.lab";
constexpr const char* MarkerContents = "SILVER_WORLD_EDITOR_LAB_V1";

bool StandaloneRequested()
{
    const char* enabled = std::getenv(StandaloneVariable);
    return enabled && std::string_view(enabled) == "1";
}

bool AuthorizedRuntime()
{
    const char* enabled = std::getenv(EnabledVariable);
    if (!enabled || std::string_view(enabled) != "1") return false;
    std::ifstream marker(MarkerName);
    std::string line;
    if (!std::getline(marker, line) || line != MarkerContents) return false;
    std::ifstream config("config.ini");
    bool localAddress = false, localPort = false;
    while (std::getline(config, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("ServerIP=")) localAddress = line == "ServerIP=localhost" || line == "ServerIP=127.0.0.1";
        if (line.starts_with("ServerPort=")) localPort = line == "ServerPort=44406";
    }
    return localAddress && localPort;
}

std::string Utf8(const wchar_t* value)
{
    if (!value || !*value) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::filesystem::path EffectCandidatePath(int mapId, int worldNumber)
{
    return std::filesystem::path("EditorCandidates") / "Effects"
        / ("Map" + std::to_string(mapId) + "-World" + std::to_string(worldNumber))
        / "effects.json";
}

std::optional<std::filesystem::path> LatestLayerSidecar(const std::filesystem::path& workspace)
{
    if (!std::filesystem::exists(workspace)) return std::nullopt;

    std::optional<std::filesystem::path> latest;
    std::filesystem::file_time_type latestTime{};
    for (const auto& entry : std::filesystem::directory_iterator(workspace))
    {
        if (entry.is_symlink() || !entry.is_directory()) continue;
        const std::string name = entry.path().filename().string();
        if (!name.starts_with("candidate-") || name.ends_with(".partial")) continue;

        const auto sidecar = entry.path() / WorldLayerSidecar::FileName;
        if (std::filesystem::is_symlink(sidecar) || !std::filesystem::is_regular_file(sidecar)) continue;
        const auto time = std::filesystem::last_write_time(sidecar);
        if (!latest || time > latestTime)
        {
            latest = sidecar;
            latestTime = time;
        }
    }
    return latest;
}

std::vector<std::uint8_t> ReadEditorSidecar(const std::filesystem::path& path, std::uintmax_t maximumBytes = 1024 * 1024)
{
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)
        || std::filesystem::file_size(path) > maximumBytes)
        throw std::runtime_error("Sidecar de Layers inseguro o fuera de limites.");

    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("No se pudo abrir el sidecar de Layers.");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::filesystem::path MonsterSpawnExportPath(int mapId)
{
    return std::filesystem::path("EditorCandidates") / "Gameplay"
        / ("Map" + std::to_string(mapId)) / "monster-spawns.json";
}
std::filesystem::path MonsterSpawnCandidatePath(int mapId)
{
    return std::filesystem::path("EditorCandidates") / "Gameplay"
        / ("Map" + std::to_string(mapId)) / WorldMonsterSpawnDocument::CandidateFileName;
}

std::filesystem::path RegionCandidatePath(int mapId)
{
    return std::filesystem::path("EditorCandidates") / "Gameplay"
        / ("Map" + std::to_string(mapId)) / WorldRegionDocument::FileName;
}

std::filesystem::path TriggerCandidatePath(int mapId)
{
    return std::filesystem::path("EditorCandidates") / "Gameplay"
        / ("Map" + std::to_string(mapId)) / WorldTriggerDocument::FileName;
}

std::filesystem::path GateExportPath(int mapId)
{
    return std::filesystem::path("EditorCandidates") / "Gameplay"
        / ("Map" + std::to_string(mapId)) / "gates.json";
}

std::vector<std::uint8_t> ReadCandidateJson(const std::filesystem::path& path)
{
    constexpr std::uintmax_t MaximumCandidateJsonBytes = 2 * 1024 * 1024;
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)
        || std::filesystem::file_size(path) > MaximumCandidateJsonBytes)
        throw std::runtime_error("Export Candidate inseguro o fuera de limites.");

    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("No se pudo abrir el export Candidate.");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<std::uint16_t> LoadedTerrainAttributes()
{
    std::vector<std::uint16_t> values(TerrainCells);
    for (int index = 0; index < TerrainCells; ++index)
        values[static_cast<std::size_t>(index)] = static_cast<std::uint16_t>(TerrainWall[index]);
    return values;
}

class WorldEditorSession
{
public:
    bool CapturesInput() const { return visible_ && editing_ && scene_.Attached(); }
    bool FreeLookEnabled() const { return freeLookEnabled_; }
    bool ToggleFreeLookHotkey();
    bool ResetCameraHotkey();
    void BootstrapStandalone();
    void Update();
    void Draw(bool editorVisible);
    void BeforeMapChange();
    void RenderPreview();
    void ApplyEffects();
    void DrawRuntimeEffects(bool* open);
    void UpdateEffectPlacement();
private:
    void RefreshMaps();
    void LoadAndOpen(const WorldMapEntry& map);
    void OpenLoadedMap(const WorldMapEntry& map);
    void CloseToSelector();
    void ReleaseLoadedState();
    void QueueMapSwitch(int index);
    void DrawGuide();
    void DrawResolutionControls();
    void DrawUniversalInspector();
    void DrawGameplayOverlays();
    void HandleRegionViewportEdit();
    void HandleSpawnViewportEdit();
    void DrawGlobalSearch();
    void DrawCommandPalette();
    void DrawPerformanceAnalyzer();
    void DrawGlobalHistory();
    void DrawChangeReview();
    void CaptureGlobalHistory();
    bool UndoGlobal();
    bool RedoGlobal();
    void ApplyWindowPreset(int index);
    void Save();
    void DrawEntry();
    bool DrawTools();
    bool DrawRibbon();
    void DrawRibbonPanels();
    void DrawRibbonFileActions(bool gesture);
    void DrawRibbonCamera();
    void DrawRibbonObjects();
    void DrawStatusBar();
    void DrawWorldOutliner();
    void DrawRegionCreation();
    void DrawTriggerCreation();
    void DrawMapValidation();
    void HandleHistoryHotkeys();
    void EnterEditorCamera();
    void LeaveEditorCamera();
    bool Dirty() const { return candidate_ && (document_.Dirty() || ui_.LayersDirty() || regions_.Dirty() || triggers_.Dirty() || monsterSpawns_.Dirty() || gates_.Dirty() || (terrainAvailable_ && terrainDocument_.Dirty()) || effects_.Dirty()); }
    std::string MapLabel(const WorldMapEntry& map) const;
    void PublishDiagnostics();

    const bool authorized_ = AuthorizedRuntime();
    bool visible_ = false, editing_ = false;
    bool standaloneBootstrapped_ = false;
    bool performanceStatsEnabled_ = false;
    bool standaloneWorldLoadPending_ = false;
    WorldObjectDocument document_;
    WorldObjectFile layerBaseline_;
    MuWorldScene scene_;
    WorldEditorUI ui_;
    WorldTerrainUI terrainUi_;
    WorldModelLibraryUI libraryUi_;
    WorldNpcLibraryUI npcLibraryUi_;
    WorldEffectsUI effectsUi_;
    WorldAiAssistantUI aiUi_;
    WorldTerrainDocument terrainDocument_;
    WorldEffectDocument effects_;
    WorldHistoryCoordinator history_;
    std::uint64_t observedObjectHistory_ = 0;
    std::uint64_t observedTerrainHistory_ = 0;
    std::uint64_t observedEffectHistory_ = 0;
    std::uint64_t observedLayerHistory_ = 0;
    std::uint64_t observedRegionHistory_ = 0;
    std::uint64_t observedTriggerHistory_ = 0;
    std::uint64_t observedSpawnHistory_ = 0;
    std::uint64_t observedGateHistory_ = 0;
    ObjectId inspectorObject_ = NoObject;
    WorldObjectRecord inspectorObjectEdit_{};
    EffectId inspectorLight_ = NoEffect;
    WorldPointLight inspectorLightEdit_{};
    bool placingEffect_ = false;
    double effectPreviewTime_ = 0;
    int runtimeEffectPreset_ = 0;
    float runtimeEffectScale_ = 1.0f;
    std::array<float,3> runtimeEffectPosition_{};
    bool runtimeEffectPositionValid_ = false;
    WorldMonsterSpawnDocument monsterSpawns_;
    SpawnId selectedSpawn_ = NoSpawn;
    SpawnId inspectorSpawn_ = NoSpawn;
    WorldMonsterSpawn inspectorSpawnEdit_{};
    int spawnDragHandle_ = -1;
    WorldMonsterSpawn spawnDragOrigin_{};
    std::array<float,3> spawnDragStartWorld_{};
    WorldRegionDocument regions_;
    RegionId selectedRegion_ = NoRegion;
    RegionId inspectorRegion_ = NoRegion;
    WorldRegion inspectorRegionEdit_{};
    int regionDragHandle_ = -1;
    WorldRegion regionDragOrigin_{};
    std::array<float, 3> regionDragStartWorld_{};
    char newRegionName_[64]{};
    char newRegionTag_[64]{};
    int newRegionShape_ = 0;
    float newRegionValues_[5]{1000.0f,1000.0f,1200.0f,1200.0f,200.0f};
    WorldTriggerDocument triggers_;
    WorldTriggerFlowUI triggerFlowUi_;
    TriggerId selectedTrigger_ = NoTrigger;
    TriggerId inspectorTrigger_ = NoTrigger;
    WorldTrigger inspectorTriggerEdit_{};
    char newTriggerName_[64]{};
    char newTriggerEvent_[96]{"PlayerEnterRegion"};
    int newTriggerRegionIndex_ = -1;
    char triggerCondition_[160]{};
    char triggerAction_[160]{};
    char globalSearch_[128]{};
    bool openGlobalSearch_ = false;
    bool openCommandPalette_ = false;
    bool showViewportGrid_ = true;
    bool showChangeReview_ = false;
    bool changeReviewValidated_ = false;
    std::uint64_t changeReviewFingerprint_ = 0;
    bool openMuBackupVerified_ = false;
    bool openMuPreflightPassed_ = false;
    std::optional<WorldOpenMuDryRunReport> openMuDryRun_;
    std::filesystem::path openMuBackupPath_;
    WorldGateDocument gates_;
    WorldGateUI gateUi_;
    GateId selectedGate_ = NoGate;
    std::optional<WorldValidationReport> validationReport_;
    std::optional<WorldCandidateVerifyReport> candidateVerifyReport_;
    std::vector<WorldCandidateVersion> candidateVersions_;
    int compareCandidateA_ = -1, compareCandidateB_ = -1;
    std::optional<WorldCandidateCompareReport> candidateCompareReport_;
    MuTerrainScene terrainScene_;
    std::unique_ptr<WorldTerrainFiles> terrainFiles_;
    std::optional<SilverWorldStudioContract> studioContract_;
    std::vector<SilverWorldStudioDiff> studioDiffs_;
    bool studioProposalApplied_ = false;
    std::optional<WorldAiCommandBatch> aiCommandBatch_;
    std::string aiCommandBatchStatus_;
    std::optional<WorldAiGameplayBatch> aiGameplayBatch_;
    std::optional<WorldAiSpawnBatch> aiSpawnBatch_;
    std::optional<WorldAiEffectBatch> aiEffectBatch_;
    std::optional<WorldAiTerrainBatch> aiTerrainBatch_;
    int ribbonTab_ = 0;
    bool toolOptionsOpen_ = true;
    int toolDetailsTab_ = 0;
    int toolLayoutRevision_ = 0;
    int toolLayoutApplied_ = -1;
    int tool_ = 0;
    int activeTool_ = 0;
    bool editorCameraActive_ = false;
    CameraMode previousCameraMode_ = CameraMode::Default;
    std::unique_ptr<WorldObjectCandidate> candidate_;
    std::string message_;

    std::vector<WorldMapEntry> maps_;
    std::vector<std::string> mapLabels_;
    std::size_t physicalWorldCount_ = 0;
    int selectedMap_ = 0;
    int pendingMap_ = -1;
    int activeMap_ = -1;
    int activeWorldNumber_ = -1;
    bool terrainAvailable_ = false;
    bool backRequested_ = false;
    int switchTarget_ = -1;
    bool openSwitchPopup_ = false;
    bool openGuidePopup_ = false;
    bool freeLookEnabled_ = true;
    int windowPreset_ = -1;
    int backgroundSwitchStage_ = 0;
    int backgroundSwitchFrames_ = 0;
    int backgroundAlternateMap_ = -1;
    bool backgroundDiagnosticPassed_ = false;
};

WorldEditorSession& Session()
{
    static WorldEditorSession session;
    return session;
}

std::string WorldEditorSession::MapLabel(const WorldMapEntry& map) const
{
    std::string name = Utf8(gMapManager.GetMapName(map.MapId));
    if (name.empty()) name = "Mapa " + std::to_string(map.MapId);
    return name + " | Map " + std::to_string(map.MapId) + " | " + map.Folder;
}

void WorldEditorSession::RefreshMaps()
{
    maps_ = WorldMapCatalog::Discover("Data");
    mapLabels_.clear();
    mapLabels_.reserve(maps_.size());
    std::set<int> physicalWorlds;
    for (const auto& map : maps_)
    {
        mapLabels_.push_back(MapLabel(map));
        physicalWorlds.insert(map.WorldNumber);
    }
    physicalWorldCount_ = physicalWorlds.size();
    if (maps_.empty())
        throw std::runtime_error("No se encontraron mapas completos editables en Data.");

    auto found = std::find_if(maps_.begin(), maps_.end(),
        [](const WorldMapEntry& entry) { return entry.MapId == gMapManager.WorldActive; });
    if (found == maps_.end())
    {
        const int currentWorld = WorldMapCatalog::PhysicalWorldNumber(gMapManager.WorldActive);
        found = std::find_if(maps_.begin(), maps_.end(),
            [currentWorld](const WorldMapEntry& entry) { return entry.WorldNumber == currentWorld; });
    }
    selectedMap_ = found == maps_.end() ? 0 : static_cast<int>(std::distance(maps_.begin(), found));
}

void WorldEditorSession::EnterEditorCamera()
{
    if (editorCameraActive_) return;
    auto& manager = CameraManager::Instance();
    previousCameraMode_ = manager.GetCurrentMode();
    if (previousCameraMode_ == CameraMode::FreeFly) previousCameraMode_ = CameraMode::Default;
    manager.SetCameraMode(CameraMode::FreeFly);
    editorCameraActive_ = manager.GetCurrentMode() == CameraMode::FreeFly;
}

void WorldEditorSession::LeaveEditorCamera()
{
    if (!editorCameraActive_) return;
    CameraManager::Instance().SetCameraMode(previousCameraMode_);
    editorCameraActive_ = false;
}

bool WorldEditorSession::ToggleFreeLookHotkey()
{
    if (!IsStandaloneMode() || !visible_ || !editing_ || !scene_.Attached())
        return false;

    freeLookEnabled_ = !freeLookEnabled_;
    message_ = freeLookEnabled_
        ? "Camara 3D libre activada. Mantene la ruedita o el boton derecho y arrastra para mirar."
        : "Camara 3D libre desactivada. WASD sigue moviendo la camara sobre el mapa.";
    return true;
}

bool WorldEditorSession::ResetCameraHotkey()
{
    if (!IsStandaloneMode() || !visible_ || !editing_ || !scene_.Attached())
        return false;

    if (!scene_.FocusMapPoint(MapEdge * 0.5f, MapEdge * 0.5f))
    {
        message_ = "No se pudo recentrar la camara del editor.";
        return true;
    }

    message_ = "Camara recentrada sobre el mapa.";
    return true;
}

void WorldEditorSession::DrawResolutionControls()
{
    if (windowPreset_ < 0)
        windowPreset_ = FindWindowPreset(WindowWidth, WindowHeight);

    const std::string windowLabel = windowPreset_ >= 0
        ? WindowPresets[windowPreset_].Label
        : std::to_string(WindowWidth) + " x " + std::to_string(WindowHeight) + " (actual)";

    ImGui::PushItemWidth(190.0f);
    if (ImGui::BeginCombo("Resolucion", windowLabel.c_str()))
    {
        for (int index = 0; index < static_cast<int>(std::size(WindowPresets)); ++index)
        {
            if (ImGui::Selectable(WindowPresets[index].Label, windowPreset_ == index))
                windowPreset_ = index;
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::BeginDisabled(windowPreset_ < 0);
    if (ImGui::Button("Aplicar resolucion"))
        ApplyWindowPreset(windowPreset_);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("Actual: %u x %u", WindowWidth, WindowHeight);
    ImGui::TextDisabled("La resolucion elegida se guarda para el proximo inicio del editor.");
}

void WorldEditorSession::ApplyWindowPreset(int index)
{
    if (!IsStandaloneMode() || index < 0 || index >= static_cast<int>(std::size(WindowPresets)))
        return;

    const auto& preset = WindowPresets[index];
    g_bUseWindowMode = TRUE;
    g_bUseFullscreenMode = FALSE;
    GameConfig::GetInstance().SetWindowMode(true);
    MuApplyWindowResolution(preset.Width, preset.Height, true);
    GameConfig::GetInstance().SetWindowSize(static_cast<int>(WindowWidth), static_cast<int>(WindowHeight));
    GameConfig::GetInstance().Save();
    windowPreset_ = FindWindowPreset(WindowWidth, WindowHeight);

    message_ = "Ventana del editor: " + std::to_string(WindowWidth)
        + " x " + std::to_string(WindowHeight) + ".";
}

void WorldEditorSession::OpenLoadedMap(const WorldMapEntry& map)
{
    if (SceneFlag != MAIN_SCENE || gMapManager.WorldActive != map.MapId
        || (!IsStandaloneMode()
            && (!szServerIpAddress || !IsAllowedEditorEndpoint(szServerIpAddress, g_ServerPort))))
        throw std::runtime_error("El mapa debe estar cargado en el cliente LAB local antes de editar.");

    const auto world = std::filesystem::path("Data") / map.Folder;
    const auto baseline = world / map.ObjectFile;
    const auto workspace = std::filesystem::path("EditorCandidates")
        / ("Map" + std::to_string(map.MapId) + "-" + map.Folder);

    auto candidate = std::make_unique<WorldObjectCandidate>(baseline, workspace);
    if (candidate->Baseline().Map != map.WorldNumber)
        throw std::runtime_error("El OBJ no corresponde al World esperado. No se abre para editar.");

    const auto studioContract = SilverWorldStudioBridge::TryLoadForMap(
        SilverWorldStudioBridge::DefaultContractRoot(), map.MapId, map.WorldNumber);
    auto documentFile = candidate->Baseline();
    std::vector<SilverWorldStudioDiff> studioDiffs;
    if (studioContract)
        studioDiffs = SilverWorldStudioBridge::Diff(*studioContract, documentFile, map.MapId, map.WorldNumber);

    const WorldObjectFile layerBaseline = documentFile;
    WorldObjectDocument document;
    document.Load(std::move(documentFile));

    WorldEffectDocument effects;
    effects.Load(EffectCandidatePath(map.MapId, map.WorldNumber), map.MapId, map.WorldNumber);

    const auto heightBytes = WorldObjectCandidate::Read(world / "TerrainHeight.OZB");
    const bool terrainAvailable = !WorldMapCatalog::UsesExtendedHeight(map.MapId)
        && TerrainHeightFile::SupportsLegacyEditing(heightBytes);
    std::unique_ptr<WorldTerrainFiles> terrainFiles;
    auto terrainDocument = std::make_unique<WorldTerrainDocument>();
    if (terrainAvailable)
    {
        const auto loadedAttributes = LoadedTerrainAttributes();
        terrainFiles = std::make_unique<WorldTerrainFiles>(
            world, map.WorldNumber, std::span<const std::uint16_t>(loadedAttributes));
        terrainFiles->OpenInto(*terrainDocument);
        terrainScene_.Attach(*terrainDocument);
    }
    else
    {
        terrainScene_.Invalidate();
    }
    scene_.Attach(document);
    const auto libraryState = workspace / WorldLibraryManifest::FileName;
    if (std::filesystem::is_regular_file(libraryState))
    {
        const auto bytes = ReadEditorSidecar(libraryState, 8 * 1024 * 1024);
        const auto saved = WorldLibraryManifest::Decode(bytes,map.MapId,candidate->BaselineSha256());
        if (!saved.MappingBytes.empty())
        {
            const auto mappingFile = world / ("EncTerrain" + std::to_string(map.WorldNumber) + ".map");
            if (!terrainAvailable || mu::crypto::Sha256FileHex(mappingFile) != saved.MappingBaseline)
                throw std::runtime_error("La base del terreno cambio; no se restaura el suelo de biblioteca sobre otra version.");
            const auto mapping = TerrainMappingCodec::Decode(saved.MappingBytes);
            if (mapping.Map != map.WorldNumber) throw std::runtime_error("El suelo guardado pertenece a otro World.");
            terrainDocument->Load(terrainDocument->Attributes(),terrainDocument->Heights(),mapping,
                map.MapId == 0 ? 123 * 256 + 135 : -1);
            terrainScene_.Synchronize(*terrainDocument);
        }
        scene_.RestoreLibrary(bytes, document, candidate->BaselineSha256());
        document.MarkSaved();
    }

    terrainFiles_ = std::move(terrainFiles);
    studioContract_ = studioContract;
    studioDiffs_ = std::move(studioDiffs);
    studioProposalApplied_ = false;
    terrainDocument_ = std::move(*terrainDocument);
    effects_ = std::move(effects);
    candidate_ = std::move(candidate);
    document_ = std::move(document);
    layerBaseline_ = layerBaseline;
    activeMap_ = map.MapId;
    activeWorldNumber_ = map.WorldNumber;
    terrainAvailable_ = terrainAvailable;
    tool_ = activeTool_ = 0;
    ui_.Reset();
    inspectorObject_ = NoObject;
    inspectorLight_ = NoEffect;
    monsterSpawns_.Clear();
    npcLibraryUi_.Cancel();
    selectedSpawn_ = NoSpawn;
    validationReport_.reset();

    std::string gameplayAdapterMessage;
    const auto spawnExport = MonsterSpawnExportPath(activeMap_);
    if (std::filesystem::exists(spawnExport))
    {
        try
        {
            monsterSpawns_.Load(ReadCandidateJson(spawnExport), activeMap_);
            const auto spawnCandidate=MonsterSpawnCandidatePath(activeMap_);
            if(std::filesystem::exists(spawnCandidate))
                monsterSpawns_.LoadCandidate(ReadCandidateJson(spawnCandidate),activeMap_);
            gameplayAdapterMessage = " MonsterSpawns OpenMU: "
                + std::to_string(monsterSpawns_.Entries().size()) + " cargados read-only; edicion aislada en Candidate.";
        }
        catch (const std::exception& error)
        {
            gameplayAdapterMessage = " MonsterSpawns ignorados por seguridad: " + std::string(error.what());
        }
    }
    else
    {
        gameplayAdapterMessage = " MonsterSpawns: falta export read-only actual de OpenMU; no se usa baseline historico.";
    }

    gates_.Clear();
    selectedGate_ = NoGate;
    const auto gateExport = GateExportPath(activeMap_);
    if (std::filesystem::exists(gateExport))
    {
        try
        {
            gates_.Load(ReadCandidateJson(gateExport), activeMap_);
            gameplayAdapterMessage += " Gates OpenMU: "
                + std::to_string(gates_.Entries().size()) + " cargados desde export actual.";
            const auto gateCandidate=gateExport.parent_path()/WorldGateDocument::CandidateFileName;
            if (std::filesystem::exists(gateCandidate))
            {
                try { gates_.LoadCandidate(ReadCandidateJson(gateCandidate)); }
                catch (const std::exception& error) { gameplayAdapterMessage += " Candidate Gates rechazado; se conserva baseline: " + std::string(error.what()); }
            }
        }
        catch (const std::exception& error)
        {
            gameplayAdapterMessage += " Gates ignorados por seguridad: " + std::string(error.what());
        }
    }
    else
    {
        gameplayAdapterMessage += " Gates: falta export read-only actual de OpenMU.";
    }

    regions_.Reset();
    selectedRegion_ = NoRegion;
    const auto regionPath = RegionCandidatePath(activeMap_);
    if (std::filesystem::exists(regionPath))
    {
        try
        {
            regions_.Decode(ReadCandidateJson(regionPath), activeMap_);
            gameplayAdapterMessage += " Regions Candidate: "
                + std::to_string(regions_.Entries().size()) + " cargadas.";
        }
        catch (const std::exception& error)
        {
            gameplayAdapterMessage += " Regions ignoradas por seguridad: " + std::string(error.what());
        }
    }

    triggers_.Reset();
    selectedTrigger_ = NoTrigger;
    const auto triggerPath = TriggerCandidatePath(activeMap_);
    if (std::filesystem::exists(triggerPath))
    {
        try
        {
            triggers_.Decode(ReadCandidateJson(triggerPath), activeMap_);
            gameplayAdapterMessage += " Triggers Candidate: "
                + std::to_string(triggers_.Entries().size()) + " cargados.";
        }
        catch (const std::exception& error)
        {
            gameplayAdapterMessage += " Triggers ignorados por seguridad: " + std::string(error.what());
        }
    }

    std::string layerRestoreMessage;
    if (const auto sidecar = LatestLayerSidecar(candidate_->Workspace()))
    {
        try
        {
            auto restored = WorldLayerSidecar::Decode(
                ReadEditorSidecar(*sidecar), layerBaseline_, map.MapId, map.WorldNumber);
            const auto restoredCount = restored.RestoredAssignments;
            const auto ambiguousCount = restored.AmbiguousAssignmentsSkipped;
            const auto missingCount = restored.MissingAssignmentsSkipped;
            ui_.RestoreLayers(std::move(restored.Layers));
            layerRestoreMessage = " Layers restauradas: " + std::to_string(restoredCount)
                + " asignaciones.";
            if (ambiguousCount || missingCount)
                layerRestoreMessage += " Omitidas seguras: " + std::to_string(ambiguousCount + missingCount) + ".";
        }
        catch (const std::exception& error)
        {
            layerRestoreMessage = " Layers sidecar ignorado por seguridad: " + std::string(error.what());
        }
    }

    effectsUi_.Reset();
    aiUi_.Reset();
    history_.Reset();
    observedObjectHistory_ = document_.HistorySerial();
    observedTerrainHistory_ = terrainDocument_.HistorySerial();
    observedEffectHistory_ = effects_.HistorySerial();
    observedLayerHistory_ = ui_.LayerHistorySerial();
    observedRegionHistory_ = regions_.HistorySerial();
    observedTriggerHistory_ = triggers_.HistorySerial();
    observedSpawnHistory_ = monsterSpawns_.HistorySerial();
    observedGateHistory_ = gates_.HistorySerial();
    editing_ = true;
    EnterEditorCamera();
    if (!scene_.FocusMapPoint(MapEdge * 0.5f, MapEdge * 0.5f))
        throw std::runtime_error("No se pudo posicionar la camara del editor sobre el mapa.");

    message_ = MapLabel(map)
        + (terrainAvailable_ ? " activo." : " activo. Altura extendida 24-bit: terreno deshabilitado, objetos habilitados.")
        + (studioContract ? " SilverWorldStudioBridge activo: "
            + std::to_string(studioContract->Placements.size()) + " placements read-only."
            : " SilverWorldStudioBridge no disponible; se usa el OBJ baseline local.")
        + layerRestoreMessage
        + gameplayAdapterMessage
        + " WASD: mover | F10: camara 3D | F11: recentrar | ruedita apretada/derecho: mirar | rueda: zoom | PageUp/PageDown: altura | Shift: rapido.";
}

void WorldEditorSession::ReleaseLoadedState()
{
    document_.Cancel();
    if (terrainAvailable_) terrainUi_.Cancel(terrainDocument_);
    ui_.CancelPlacement();
    scene_.SetPlacementPreview(std::nullopt);
    scene_.Invalidate();
    terrainScene_.Invalidate();
    ui_.Reset();
    effectsUi_.Reset();
    aiUi_.Reset();
    history_.Reset();
    observedObjectHistory_ = observedTerrainHistory_ = observedEffectHistory_ = observedLayerHistory_ = observedRegionHistory_ = observedTriggerHistory_ = observedSpawnHistory_ = observedGateHistory_ = 0;
    studioContract_.reset();
    terrainFiles_.reset();
    effects_.Clear();
    inspectorObject_ = NoObject;
    inspectorLight_ = NoEffect;
    monsterSpawns_.Clear();
    npcLibraryUi_.Cancel();
    selectedSpawn_ = NoSpawn;
    gates_.Clear();
    selectedGate_ = NoGate;
    regions_.Reset();
    selectedRegion_ = NoRegion;
    inspectorRegion_ = NoRegion;
    triggers_.Reset();
    selectedTrigger_ = NoTrigger;
    validationReport_.reset();
    candidate_.reset();
    layerBaseline_ = {};
    terrainAvailable_ = false;
    editing_ = false;
    activeMap_ = -1;
    activeWorldNumber_ = -1;
    backRequested_ = false;
    LeaveEditorCamera();
}

void WorldEditorSession::QueueMapSwitch(int index)
{
    if (index < 0 || index >= static_cast<int>(maps_.size()))
    {
        message_ = "Seleccion de mapa invalida.";
        return;
    }

    if (Dirty())
    {
        switchTarget_ = index;
        openSwitchPopup_ = true;
        message_ = "Hay cambios sin guardar. Elegi guardar, descartar o cancelar antes de cambiar de mapa.";
        return;
    }

    ReleaseLoadedState();
    pendingMap_ = index;
}

void WorldEditorSession::DrawGuide()
{
    if (openGuidePopup_)
    {
        ImGui::OpenPopup("Guia rapida | Silver World Editor");
        openGuidePopup_ = false;
    }

    if (ImGui::BeginPopupModal("Guia rapida | Silver World Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SeparatorText("Flujo seguro");
        ImGui::BulletText("Elegir mapa -> editar -> guardar copia de prueba -> validar candidate.");
        ImGui::BulletText("Cambiar de mapa nunca sobrescribe Data original.");
        ImGui::BulletText("Con cambios pendientes podes Guardar y cambiar, Descartar y cambiar o Cancelar.");

        ImGui::SeparatorText("Camara y seleccion");
        ImGui::BulletText("W/A/S/D o flechas: mover | ruedita apretada/derecho: mirar | rueda: zoom.");
        ImGui::BulletText("PageUp/PageDown: altura | Shift: movimiento rapido | F: enfocar seleccion.");
        ImGui::BulletText("Ctrl+clic: seleccion multiple | Esc: cancelar preview/gesto.");

        ImGui::SeparatorText("Herramientas");
        ImGui::BulletText("Objetos: Asset Browser, gizmo, inspector, Undo/Redo y placement.");
        ImGui::BulletText("Terreno: altura, zonas y materiales cuando el mapa admite edicion legacy.");
        ImGui::BulletText("Biblioteca MU: inventario global de ObjectN, inspeccion e import/export no destructivo.");
        ImGui::BulletText("Efectos y luces: se habilitan solo mediante adapters soportados; nada se serializa a ciegas.");
        ImGui::BulletText("Asistente IA: propone; vos revisas preview y confirmas.");

        if (ImGui::Button("Cerrar guia"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void WorldEditorSession::LoadAndOpen(const WorldMapEntry& map)
{
    if (!IsStandaloneMode()
        && (SceneFlag != MAIN_SCENE || !szServerIpAddress
            || !IsAllowedEditorEndpoint(szServerIpAddress, g_ServerPort)))
        throw std::runtime_error("Entra con un personaje al Silver Core LOCAL antes de cargar un mapa.");

    if (Dirty())
        throw std::runtime_error("Hay cambios sin guardar. Usa Guardar y cambiar, Descartar y cambiar o Cancelar.");

    // Map changes replace textures/models used by the previous frame. This function
    // is called only from the pre-frame hook; wait for all submitted GPU work before
    // releasing or replacing those resources.
    mu::WaitForSDLGpuIdle();

    // Always release the previous map/editor state before loading another one.
    // This prevents stale candidate, terrain, camera, preview and selection state
    // from leaking into the next WorldN session.
    ReleaseLoadedState();

    g_ErrorReport.Write(L"> World Editor pre-frame: loading map %d.\r\n", map.MapId);
    gMapManager.WorldActive = map.MapId;
    gMapManager.LoadWorld(map.MapId);
    if (gMapManager.WorldActive != map.MapId)
        throw std::runtime_error("El cliente normalizo el mapa a otro ID; no se abre el editor.");

    OpenLoadedMap(map);
}

void WorldEditorSession::Save()
{
    if (!candidate_) return;
    if (document_.HasPreview() || (terrainAvailable_ && terrainDocument_.Active()) || ui_.Placing())
        throw std::runtime_error("Termina el gesto antes de guardar.");

    if (activeMap_ < 0 || activeWorldNumber_ < 0)
        throw std::runtime_error("Se perdio la identidad del mapa del borrador; no se guarda el candidate.");

    auto assets = terrainFiles_
        ? terrainFiles_->Export(terrainDocument_)
        : std::vector<WorldCandidateAsset>{};

    const auto layerSidecar = WorldLayerSidecar::Encode(
        layerBaseline_, ui_.Layers(), activeMap_, activeWorldNumber_);
    WorldCandidateAsset layerAsset;
    layerAsset.Name = WorldLayerSidecar::FileName;
    layerAsset.Updated = layerSidecar.Bytes;
    assets.push_back(std::move(layerAsset));

    const auto output = candidate_->Save(scene_.ExportNativeObjects(document_), assets);
    const auto libraryOutput = candidate_->Workspace() / WorldLibraryManifest::FileName;
    const auto libraryBytes = scene_.ExportLibrary(document_, candidate_->BaselineSha256(),
        terrainDocument_.Mapping() ? &*terrainDocument_.Mapping() : nullptr);
    GateCandidateFile::Save(libraryOutput, libraryBytes);
    const auto effectsOutput = EffectCandidatePath(activeMap_, activeWorldNumber_);
    effects_.Save(effectsOutput, activeMap_, activeWorldNumber_);

    const auto regionOutput = RegionCandidatePath(activeMap_);
    std::filesystem::create_directories(regionOutput.parent_path());
    const auto regionTemp = regionOutput.string() + ".partial";
    const auto regionBytes = regions_.Encode(activeMap_);
    {
        std::ofstream stream(regionTemp, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("No se pudo preparar Regions Candidate.");
        stream.write(reinterpret_cast<const char*>(regionBytes.data()),
            static_cast<std::streamsize>(regionBytes.size()));
        if (!stream) throw std::runtime_error("No se pudo completar Regions Candidate.");
    }
    std::error_code regionError;
    std::filesystem::remove(regionOutput, regionError);
    regionError.clear();
    std::filesystem::rename(regionTemp, regionOutput, regionError);
    if (regionError)
    {
        std::filesystem::remove(regionTemp);
        throw std::runtime_error("No se pudo publicar Regions Candidate.");
    }

    const auto triggerOutput = TriggerCandidatePath(activeMap_);
    std::filesystem::create_directories(triggerOutput.parent_path());
    const auto triggerTemp = triggerOutput.string() + ".partial";
    const auto triggerBytes = triggers_.Encode(activeMap_);
    {
        std::ofstream stream(triggerTemp, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("No se pudo preparar Triggers Candidate.");
        stream.write(reinterpret_cast<const char*>(triggerBytes.data()),
            static_cast<std::streamsize>(triggerBytes.size()));
        if (!stream) throw std::runtime_error("No se pudo completar Triggers Candidate.");
    }
    std::error_code triggerError;
    std::filesystem::remove(triggerOutput, triggerError);
    triggerError.clear();
    std::filesystem::rename(triggerTemp, triggerOutput, triggerError);
    if (triggerError)
    {
        std::filesystem::remove(triggerTemp);
        throw std::runtime_error("No se pudo publicar Triggers Candidate.");
    }

    std::filesystem::path spawnOutput;
    if(monsterSpawns_.Loaded())
    {
        spawnOutput=MonsterSpawnCandidatePath(activeMap_);
        std::filesystem::create_directories(spawnOutput.parent_path());
        const auto spawnTemp=spawnOutput.string()+".partial";
        const auto spawnBytes=monsterSpawns_.EncodeCandidate();
        {std::ofstream stream(spawnTemp,std::ios::binary|std::ios::trunc);if(!stream)throw std::runtime_error("No se pudo preparar MonsterSpawns Candidate.");stream.write(reinterpret_cast<const char*>(spawnBytes.data()),static_cast<std::streamsize>(spawnBytes.size()));if(!stream)throw std::runtime_error("No se pudo completar MonsterSpawns Candidate.");}
        std::error_code spawnError;std::filesystem::remove(spawnOutput,spawnError);spawnError.clear();std::filesystem::rename(spawnTemp,spawnOutput,spawnError);
        if(spawnError){std::filesystem::remove(spawnTemp);throw std::runtime_error("No se pudo publicar MonsterSpawns Candidate.");}
        monsterSpawns_.MarkSaved();
    }

    std::filesystem::path gateOutput;
    if (gates_.Loaded())
    {
        gateOutput=GateExportPath(activeMap_).parent_path()/WorldGateDocument::CandidateFileName;
        GateCandidateFile::Save(gateOutput,gates_.EncodeCandidate());
    }
    const auto candidateDir=output.parent_path();
    const auto copyIntoCandidate=[&](const std::filesystem::path& source,const std::string& name)
    {
        if(!std::filesystem::is_regular_file(source))throw std::runtime_error("Sidecar Candidate requerido ausente: "+source.string());
        const auto bytes=name==WorldLibraryManifest::FileName ? ReadEditorSidecar(source,8*1024*1024) : ReadCandidateJson(source);const auto target=candidateDir/name;
        std::ofstream stream(target,std::ios::binary|std::ios::trunc);if(!stream)throw std::runtime_error("No se pudo empaquetar sidecar Candidate: "+name);
        stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!stream)throw std::runtime_error("Sidecar Candidate empaquetado incompleto: "+name);
    };
    copyIntoCandidate(libraryOutput, WorldLibraryManifest::FileName);
    copyIntoCandidate(effectsOutput,"effects.json");copyIntoCandidate(regionOutput,WorldRegionDocument::FileName);copyIntoCandidate(triggerOutput,WorldTriggerDocument::FileName);
    if(!spawnOutput.empty())copyIntoCandidate(spawnOutput,WorldMonsterSpawnDocument::CandidateFileName);
    if(!gateOutput.empty())copyIntoCandidate(gateOutput,WorldGateDocument::CandidateFileName);

    WorldCandidateManifest manifest;manifest.MapId=activeMap_;manifest.WorldNumber=activeWorldNumber_;manifest.ObjectBaselineSha256=candidate_->BaselineSha256();if(terrainFiles_)manifest.TerrainBaselines=terrainFiles_->BaselineFingerprints();
    const auto hashFile=[](const std::filesystem::path& path){return mu::crypto::Sha256FileHex(path);};
    manifest.Sidecars.emplace_back(WorldLayerSidecar::FileName,mu::crypto::Sha256Hex(std::string_view(reinterpret_cast<const char*>(layerSidecar.Bytes.data()),layerSidecar.Bytes.size())));
    manifest.Sidecars.emplace_back(WorldLibraryManifest::FileName,hashFile(candidateDir/WorldLibraryManifest::FileName));
    manifest.Sidecars.emplace_back("effects.json",hashFile(candidateDir/"effects.json"));manifest.Sidecars.emplace_back(WorldRegionDocument::FileName,hashFile(candidateDir/WorldRegionDocument::FileName));manifest.Sidecars.emplace_back(WorldTriggerDocument::FileName,hashFile(candidateDir/WorldTriggerDocument::FileName));
    if(!spawnOutput.empty()){manifest.Sidecars.emplace_back(WorldMonsterSpawnDocument::CandidateFileName,hashFile(candidateDir/WorldMonsterSpawnDocument::CandidateFileName));const auto baselinePath=MonsterSpawnExportPath(activeMap_);if(std::filesystem::is_regular_file(baselinePath))manifest.OpenMuBaselineSha256=hashFile(baselinePath);}
    if(!gateOutput.empty())manifest.Sidecars.emplace_back(WorldGateDocument::CandidateFileName,hashFile(candidateDir/WorldGateDocument::CandidateFileName));
    const auto manifestBytes=manifest.Encode();const auto manifestPath=output.parent_path()/"candidate-manifest.json";{std::ofstream stream(manifestPath,std::ios::binary|std::ios::trunc);if(!stream)throw std::runtime_error("No se pudo crear Candidate Manifest.");stream.write(reinterpret_cast<const char*>(manifestBytes.data()),static_cast<std::streamsize>(manifestBytes.size()));if(!stream)throw std::runtime_error("Candidate Manifest incompleto.");}

    document_.MarkSaved();
    regions_.MarkSaved();
    triggers_.MarkSaved();
    if (gates_.Loaded()) gates_.MarkSaved();
    ui_.MarkLayersSaved();
    if (terrainAvailable_) terrainDocument_.MarkSaved();
    message_ = "Copia guardada: " + output.generic_string()
        + " | efectos: " + effectsOutput.generic_string()
        + " | Layers persistentes: " + std::to_string(layerSidecar.PersistentAssignments)
        + " | Layers ambiguas omitidas: " + std::to_string(layerSidecar.AmbiguousAssignmentsSkipped)
        + " | Regions: " + std::to_string(regions_.Entries().size())
        + " | Triggers: " + std::to_string(triggers_.Entries().size());
    ui_.SetStatus(message_);
}

void WorldEditorSession::CloseToSelector()
{
    const bool keepDraft = Dirty();

    document_.Cancel();
    if (terrainAvailable_) terrainUi_.Cancel(terrainDocument_);
    ui_.CancelPlacement();
    scene_.SetPlacementPreview(std::nullopt);
    scene_.Invalidate();
    terrainScene_.Invalidate();
    ui_.Reset();
    aiUi_.Reset();
    studioContract_.reset();
    editing_ = false;
    backRequested_ = false;
    LeaveEditorCamera();

    if (!keepDraft)
    {
        activeMap_ = -1;
        activeWorldNumber_ = -1;
        terrainFiles_.reset();
        effects_.Clear();
        effectsUi_.Reset();
        candidate_.reset();
        layerBaseline_ = {};
        terrainAvailable_ = false;
    }

    message_ = keepDraft
        ? "Volviste al selector con cambios pendientes. Podes guardarlos, descartarlos al cambiar de mapa o cancelar."
        : "Volviste al selector. Podes cargar el mismo mapa de nuevo o elegir otro.";
}

void WorldEditorSession::BeforeMapChange()
{
    if (!authorized_) return;

    if (Dirty())
    {
        document_.Cancel();
        if (terrainAvailable_) terrainUi_.Cancel(terrainDocument_);
        ui_.CancelPlacement();
        scene_.SetPlacementPreview(std::nullopt);
        scene_.Invalidate();
        terrainScene_.Invalidate();
        ui_.Reset();
        aiUi_.Reset();
        editing_ = false;
        LeaveEditorCamera();
        message_ = "Cambio externo de mapa detectado: el borrador queda conservado hasta guardar o descartar.";
        return;
    }

    ReleaseLoadedState();
}

void WorldEditorSession::BootstrapStandalone()
{
    if (!authorized_ || !StandaloneRequested() || standaloneBootstrapped_)
        return;

    standaloneBootstrapped_ = true;
    visible_ = true;
    editing_ = false;
    message_ = "Modo standalone: elegi un mapa. No requiere cuenta ni GameServer.";

    try
    {
        RefreshMaps();
        gMapManager.WorldActive = 0;
        SceneFlag = MAIN_SCENE;
        InitMainScene = true;
        EnableMainRender = true;
        LoadingWorld = 0;
        standaloneWorldLoadPending_ = true;

        // Never mutate world/GPU resources from WebzenScene rendering. The base
        // Lorencia world and any editable-map request are consumed by the next
        // pre-frame editor update, before SDL_GPU BeginFrame acquires a command buffer.
        // Hidden diagnostics then exercise the same editable-map open path as the
        // visible "Cargar mapa seleccionado" action.
        if (BackgroundEditorRequested())
        {
            const auto lorencia = std::find_if(maps_.begin(), maps_.end(),
                [](const WorldMapEntry& entry) { return entry.MapId == 0; });
            if (lorencia == maps_.end())
                throw std::runtime_error("Lorencia no esta disponible para el diagnostico oculto.");
            pendingMap_ = static_cast<int>(std::distance(maps_.begin(), lorencia));
            backgroundSwitchStage_ = 1;
            backgroundSwitchFrames_ = 0;
            backgroundAlternateMap_ = -1;
            backgroundDiagnosticPassed_ = false;
            g_ErrorReport.Write(L"> Diagnostico multi-mapa START: Lorencia -> otro mapa -> Lorencia.\r\n");
        }
    }
    catch (const std::exception& error)
    {
        message_ = std::string{"No se pudo iniciar el editor standalone: "} + error.what();
    }
}

void WorldEditorSession::DrawEntry()
{
    ImGui::SetNextWindowPos({320, 64}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({660, 500}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Silver World Editor 3D | selector de mapas"))
    {
        ImGui::TextWrapped("Elegí un mapa y cargalo como vista local de edición. Esto cambia el mapa visible del cliente LAB, pero no mueve tu personaje en el servidor ni toca SQL.");
        ImGui::TextWrapped("Después usa F12 para mostrar/ocultar herramientas. Al volver a este selector podes cargar otra vez el mismo mapa: se recarga desde Data y no queda enganchado el estado anterior.");

        ImGui::SeparatorText("Resolucion del editor");
        DrawResolutionControls();
        ImGui::SeparatorText("Mapa");

        if (maps_.empty())
        {
            try { RefreshMaps(); }
            catch (const std::exception& error) { message_ = error.what(); }
        }

        if (!maps_.empty())
        {
            const char* preview = mapLabels_[static_cast<std::size_t>(selectedMap_)].c_str();
            if (ImGui::BeginCombo("Mapa para editar", preview))
            {
                for (int index = 0; index < static_cast<int>(maps_.size()); ++index)
                    if (ImGui::Selectable(mapLabels_[static_cast<std::size_t>(index)].c_str(), index == selectedMap_))
                        selectedMap_ = index;
                ImGui::EndCombo();
            }

            ImGui::Text("%zu mapas logicos | %zu carpetas WorldN fisicas", maps_.size(), physicalWorldCount_);
            if (ImGui::Button("Cargar mapa seleccionado"))
                QueueMapSwitch(selectedMap_);
            ImGui::SameLine();
            if (ImGui::Button("Actualizar lista de mapas"))
            {
                try { RefreshMaps(); message_ = "Lista de mapas actualizada."; }
                catch (const std::exception& error) { message_ = error.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Guia rapida"))
                openGuidePopup_ = true;
        }

        if (openSwitchPopup_)
        {
            ImGui::OpenPopup("Cambiar mapa con cambios pendientes");
            openSwitchPopup_ = false;
        }
        if (ImGui::BeginPopupModal("Cambiar mapa con cambios pendientes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const std::string target = switchTarget_ >= 0 && switchTarget_ < static_cast<int>(mapLabels_.size())
                ? mapLabels_[static_cast<std::size_t>(switchTarget_)]
                : std::string{"mapa seleccionado"};
            ImGui::TextWrapped("Hay cambios sin guardar. Destino: %s", target.c_str());
            ImGui::TextWrapped("Guardar crea una copia candidate; Descartar elimina solo el borrador en memoria. Data original no se toca.");

            if (ImGui::Button("Guardar y cambiar"))
            {
                try
                {
                    Save();
                    const int targetIndex = switchTarget_;
                    switchTarget_ = -1;
                    ImGui::CloseCurrentPopup();
                    QueueMapSwitch(targetIndex);
                }
                catch (const std::exception& error) { message_ = error.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Descartar y cambiar"))
            {
                const int targetIndex = switchTarget_;
                switchTarget_ = -1;
                ReleaseLoadedState();
                pendingMap_ = targetIndex;
                message_ = "Borrador descartado en memoria. Cargando mapa seleccionado...";
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar"))
            {
                switchTarget_ = -1;
                message_ = "Cambio de mapa cancelado.";
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (Dirty())
        {
            ImGui::SeparatorText("Borrador conservado");
            ImGui::TextWrapped("Hay cambios pendientes. Podes guardarlos ahora o elegir otro mapa y decidir Guardar/Descartar/Cancelar.");
            if (ImGui::Button("Guardar borrador conservado"))
            {
                try { Save(); } catch (const std::exception& error) { message_ = error.what(); }
            }
        }

        DrawGuide();
        ImGui::SeparatorText("Controles de camara");
        ImGui::TextWrapped("W/A/S/D o flechas: mover | ruedita apretada o boton derecho + arrastrar: mirar | rueda: zoom | PageUp/PageDown: subir/bajar | Shift: movimiento rapido.");
        ImGui::TextWrapped("%s", message_.c_str());
    }
    ImGui::End();
}

void WorldEditorSession::DrawCommandPalette()
{
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift
        && ImGui::IsKeyPressed(ImGuiKey_P, false)) openCommandPalette_ = true;
    if(openCommandPalette_){ ImGui::OpenPopup("Command Palette"); openCommandPalette_=false; }
    if(!ImGui::BeginPopup("Command Palette")) return;
    ImGui::TextDisabled("Silver World Editor commands");
    if(ImGui::Selectable("Revisar cambios Candidate")){ showChangeReview_=true; ImGui::CloseCurrentPopup(); }
    if(ImGui::Selectable("Validar mapa / Check Map"))
    {
        validationReport_=WorldMapValidator::Validate(document_,effects_,&monsterSpawns_,&gates_,&regions_,&triggers_,&ui_.Layers(),terrainAvailable_?&terrainDocument_:nullptr);
        message_="Check Map ejecutado desde Command Palette.";
        ImGui::CloseCurrentPopup();
    }
    if(ImGui::Selectable("Ir a Objects")){ tool_=activeTool_=0; ImGui::CloseCurrentPopup(); }
    ImGui::BeginDisabled(!terrainAvailable_);
    if(ImGui::Selectable("Ir a Terrain")){ tool_=activeTool_=1; ImGui::CloseCurrentPopup(); }
    ImGui::EndDisabled();
    if(ImGui::Selectable("Ir a Biblioteca MU")){ tool_=activeTool_=2; ImGui::CloseCurrentPopup(); }
    if(ImGui::Selectable("Ir a Effects / Lights")){ tool_=activeTool_=3; ImGui::CloseCurrentPopup(); }
    if(ImGui::Selectable("Ir a Silver AI")){ tool_=activeTool_=4; ImGui::CloseCurrentPopup(); }
    if(ui_.Selected()!=NoObject && ImGui::Selectable("Enfocar objeto seleccionado"))
    {
        scene_.Focus(ui_.Selected()); ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}


void WorldEditorSession::DrawChangeReview()
{
    if(!showChangeReview_)return;
    if(!ImGui::Begin("Candidate Change Review",&showChangeReview_)){ImGui::End();return;}
    ImGui::TextDisabled("Revision previa: este panel no aplica cambios a OpenMU ni Data original.");
    if(candidate_)
    {
        const auto manifestPath=candidate_->Workspace()/"candidate-manifest.json";
        if(ImGui::Button("Verificar ultimo Candidate Manifest"))
        {
            try
            {
                if(!std::filesystem::is_regular_file(manifestPath))throw std::runtime_error("No existe candidate-manifest.json en el workspace actual.");
                const auto manifest=WorldCandidateManifest::Decode(ReadCandidateJson(manifestPath));
                candidateVerifyReport_=WorldCandidateVerifier::Verify(manifest,manifestPath.parent_path(),candidate_->BaselinePath().parent_path());
            }
            catch(const std::exception& error){candidateVerifyReport_=WorldCandidateVerifyReport{};candidateVerifyReport_->State=WorldCandidateVerifyState::Missing;candidateVerifyReport_->Issues.push_back({WorldCandidateVerifyState::Missing,"candidate-manifest.json",error.what()});}
        }
        if(candidateVerifyReport_)
        {
            ImGui::SameLine();ImGui::Text("Provenance: %s",WorldCandidateVerifier::StateName(candidateVerifyReport_->State));
            for(const auto& issue:candidateVerifyReport_->Issues)ImGui::BulletText("%s | %s | %s",WorldCandidateVerifier::StateName(issue.State),issue.File.c_str(),issue.Message.c_str());
        }
        ImGui::SameLine();
        if(ImGui::Button("Versiones"))candidateVersions_=WorldCandidateBrowser::Discover(candidate_->Workspace());
        if(!candidateVersions_.empty()&&ImGui::TreeNodeEx("Candidate Version History",ImGuiTreeNodeFlags_DefaultOpen))
        {
            for(const auto& version:candidateVersions_)
            {
                const auto name=version.Directory.filename().string();
                ImGui::PushID(name.c_str());
                if(version.Manifest)
                {
                    const auto report=WorldCandidateVerifier::Verify(*version.Manifest,version.Directory,candidate_->BaselinePath().parent_path());
                    ImGui::Text("%s | %s | %zu files | Map %d / World %d",name.c_str(),WorldCandidateVerifier::StateName(report.State),version.Files,version.Manifest->MapId,version.Manifest->WorldNumber);
                    if(!report.Verified()&&ImGui::TreeNode("issues")){for(const auto& issue:report.Issues)ImGui::BulletText("%s | %s",issue.File.c_str(),issue.Message.c_str());ImGui::TreePop();}
                }
                else ImGui::Text("%s | MANIFEST MISSING/INVALID | %zu files",name.c_str(),version.Files);
                ImGui::PopID();
            }
            ImGui::SeparatorText("Comparar versiones");
            auto combo=[&](const char* label,int& selected){const char* preview=(selected>=0&&selected<static_cast<int>(candidateVersions_.size()))?candidateVersions_[static_cast<std::size_t>(selected)].Directory.filename().string().c_str():"Seleccionar";if(ImGui::BeginCombo(label,preview)){for(int i=0;i<static_cast<int>(candidateVersions_.size());++i){const auto n=candidateVersions_[static_cast<std::size_t>(i)].Directory.filename().string();if(ImGui::Selectable(n.c_str(),selected==i))selected=i;}ImGui::EndCombo();}};
            combo("Version A",compareCandidateA_);combo("Version B",compareCandidateB_);
            ImGui::BeginDisabled(compareCandidateA_<0||compareCandidateB_<0||compareCandidateA_==compareCandidateB_);
            if(ImGui::Button("Comparar A vs B"))candidateCompareReport_=WorldCandidateCompare::Compare(candidateVersions_[static_cast<std::size_t>(compareCandidateA_)],candidateVersions_[static_cast<std::size_t>(compareCandidateB_)]);
            ImGui::EndDisabled();
            if(candidateCompareReport_){ImGui::Text("Contenido A => B | Objects %zu=>%zu | Regions %zu=>%zu | Triggers %zu=>%zu | Lights %zu=>%zu | RuntimeFx %zu=>%zu",candidateCompareReport_->LeftSummary.Objects,candidateCompareReport_->RightSummary.Objects,candidateCompareReport_->LeftSummary.Regions,candidateCompareReport_->RightSummary.Regions,candidateCompareReport_->LeftSummary.Triggers,candidateCompareReport_->RightSummary.Triggers,candidateCompareReport_->LeftSummary.Lights,candidateCompareReport_->RightSummary.Lights,candidateCompareReport_->LeftSummary.RuntimeEffects,candidateCompareReport_->RightSummary.RuntimeEffects);if(!candidateCompareReport_->SemanticChanges.empty()){ImGui::SeparatorText("Semantic Diff");for(const auto& [domain,stats]:candidateCompareReport_->Stats)ImGui::Text("%s | +%zu ~%zu -%zu",domain.c_str(),stats.Added,stats.Edited,stats.Deleted);static char diffFilter[64]{};ImGui::InputTextWithHint("##candidate-diff-filter","Filtrar dominio/texto...",diffFilter,sizeof(diffFilter));const std::string filter=diffFilter;for(const auto& change:candidateCompareReport_->SemanticChanges)if(filter.empty()||change.Text.find(filter)!=std::string::npos){ImGui::PushID(change.Text.c_str());ImGui::BulletText("%s",change.Text.c_str());bool selectable=false;if(change.Domain=="Region"&&change.Id&&regions_.Find(change.Id)){selectable=true;if(ImGui::SmallButton("Seleccionar Region")){selectedRegion_=change.Id;tool_=activeTool_=6;}}else if(change.Domain=="Trigger"&&change.Id&&triggers_.Find(change.Id)){selectable=true;if(ImGui::SmallButton("Seleccionar Trigger")){selectedTrigger_=change.Id;tool_=activeTool_=7;}}else if(change.Domain=="MonsterSpawn"&&change.Id&&monsterSpawns_.Find(change.Id)){selectable=true;if(ImGui::SmallButton("Seleccionar Spawn")){selectedSpawn_=change.Id;tool_=activeTool_=5;}}else if(change.Domain=="Light"&&change.Id&&effects_.Find(change.Id)){selectable=true;if(ImGui::SmallButton("Seleccionar Light")){effectsUi_.SelectById(effects_,change.Id);tool_=activeTool_=3;inspectorLight_=NoEffect;}}else if(change.Domain=="RuntimeEffect"&&change.Id&&effects_.FindRuntimeEffect(change.Id)){selectable=true;if(ImGui::SmallButton("Ir a Runtime Effect")){const auto* fx=effects_.FindRuntimeEffect(change.Id);if(fx)scene_.FocusMapPoint(fx->Position[0],fx->Position[1]);tool_=activeTool_=3;}}else if(change.Domain=="Object"&&change.Index>=0&&static_cast<std::size_t>(change.Index)<document_.Entries().size()){selectable=true;if(ImGui::SmallButton("Seleccionar Object")){const auto& e=document_.Entries()[static_cast<std::size_t>(change.Index)];if(e.Record)ui_.SelectOnly(e.Id);}}else if(change.Domain=="Terrain"&&change.MinX>=0){selectable=true;if(ImGui::SmallButton("Ir a zona Terrain")){const float cx=(change.MinX+change.MaxX+1)*50.0f,cy=(change.MinY+change.MaxY+1)*50.0f;scene_.FocusMapPoint(cx,cy);}}(void)selectable;ImGui::PopID();}}if(candidateCompareReport_->Identical())ImGui::TextUnformatted("Manifest/contenido: sin diferencias.");else{ImGui::Text("%zu diferencias de procedencia",candidateCompareReport_->Differences.size());for(const auto& d:candidateCompareReport_->Differences)ImGui::BulletText("%s | %s | %.16s... => %.16s...",d.Domain.c_str(),d.Name.c_str(),d.Left.c_str(),d.Right.c_str());}}
            ImGui::TreePop();
        }
    }
    const std::uint64_t reviewFingerprint=document_.Revision()
        ^ (terrainDocument_.Revision()<<1) ^ (effects_.HistorySerial()<<2)
        ^ (ui_.LayerHistorySerial()<<3) ^ (regions_.HistorySerial()<<4)
        ^ (triggers_.HistorySerial()<<5) ^ (monsterSpawns_.HistorySerial()<<6) ^ (gates_.Revision()<<7);
    if(changeReviewValidated_&&reviewFingerprint!=changeReviewFingerprint_){changeReviewValidated_=false;openMuBackupVerified_=false;openMuPreflightPassed_=false;openMuDryRun_.reset();}
    const auto spawnChanges=monsterSpawns_.ChangedIds();
    ImGui::Text("Monster/NPC Spawns: %zu cambios Candidate",spawnChanges.size());
    if(monsterSpawns_.Loaded()&&spawnChanges.empty())ImGui::TextDisabled("Sin diferencias contra el export OpenMU actual.");
    for(const auto id:spawnChanges)
    {
        const auto* before=monsterSpawns_.FindBaseline(id);const auto* after=monsterSpawns_.Find(id);if(!after)continue;
        if(!before)
        {
            ImGui::BulletText("NUEVO %s [%d] | %s | %u,%u | cantidad %d",after->Designation.c_str(),after->MonsterNumber,after->MapVariant.c_str(),after->X1,after->Y1,after->Quantity);
            continue;
        }
        ImGui::PushID(static_cast<int>(id));
        const std::string title=after->ObjectKind+" #"+std::to_string(id)+" | "+after->Designation+" ["+std::to_string(after->MonsterNumber)+"]";
        if(ImGui::TreeNodeEx("spawn-diff",ImGuiTreeNodeFlags_SpanAvailWidth,"%s",title.c_str()))
        {
            if(before->X1!=after->X1||before->Y1!=after->Y1||before->X2!=after->X2||before->Y2!=after->Y2)
                ImGui::BulletText("Area: (%u,%u)->(%u,%u)  =>  (%u,%u)->(%u,%u)",before->X1,before->Y1,before->X2,before->Y2,after->X1,after->Y1,after->X2,after->Y2);
            if(before->Quantity!=after->Quantity)ImGui::BulletText("Quantity: %d => %d",before->Quantity,after->Quantity);
            if(before->Direction!=after->Direction)ImGui::BulletText("Direction: %u => %u",before->Direction,after->Direction);
            if(before->SpawnTrigger!=after->SpawnTrigger)ImGui::BulletText("SpawnTrigger: %s => %s",before->SpawnTrigger.c_str(),after->SpawnTrigger.c_str());
            if(before->WaveNumber!=after->WaveNumber)ImGui::BulletText("Wave: %u => %u",before->WaveNumber,after->WaveNumber);
            if(before->RespawnDelayMilliseconds!=after->RespawnDelayMilliseconds)ImGui::BulletText("Respawn: %lld => %lld ms",static_cast<long long>(before->RespawnDelayMilliseconds),static_cast<long long>(after->RespawnDelayMilliseconds));
            if(ImGui::SmallButton("Seleccionar en WORLD")){selectedSpawn_=id;tool_=activeTool_=5;}
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    gateUi_.DrawChanges(gates_);
    if(ImGui::TreeNodeEx("Regions diff",ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& saved=regions_.SavedEntries();
        for(const auto& current:regions_.Entries())
        {
            const auto it=std::find_if(saved.begin(),saved.end(),[&](const auto& x){return x.Id==current.Id;});
            if(it==saved.end())ImGui::BulletText("ADD Region #%llu | %s",static_cast<unsigned long long>(current.Id),current.Name.c_str());
            else if(!(current==*it))ImGui::BulletText("EDIT Region #%llu | %s",static_cast<unsigned long long>(current.Id),current.Name.c_str());
        }
        for(const auto& before:saved)if(!regions_.Find(before.Id))ImGui::BulletText("DELETE Region #%llu | %s",static_cast<unsigned long long>(before.Id),before.Name.c_str());
        ImGui::TreePop();
    }
    if(ImGui::TreeNodeEx("Triggers diff",ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& saved=triggers_.SavedEntries();
        for(const auto& current:triggers_.Entries())
        {
            const auto it=std::find_if(saved.begin(),saved.end(),[&](const auto& x){return x.Id==current.Id;});
            if(it==saved.end())ImGui::BulletText("ADD Trigger #%llu | %s",static_cast<unsigned long long>(current.Id),current.Name.c_str());
            else if(!(current==*it))
            {
                ImGui::BulletText("EDIT Trigger #%llu | %s",static_cast<unsigned long long>(current.Id),current.Name.c_str());
                if(current.Event!=it->Event)ImGui::Text("  Event: %s => %s",it->Event.c_str(),current.Event.c_str());
                if(current.Region!=it->Region)ImGui::Text("  Region: %llu => %llu",static_cast<unsigned long long>(it->Region),static_cast<unsigned long long>(current.Region));
                if(current.Enabled!=it->Enabled)ImGui::Text("  Enabled: %s => %s",it->Enabled?"true":"false",current.Enabled?"true":"false");
                if(current.Conditions!=it->Conditions)ImGui::Text("  Conditions: %zu => %zu",it->Conditions.size(),current.Conditions.size());
                if(current.Actions!=it->Actions)ImGui::Text("  Actions: %zu => %zu",it->Actions.size(),current.Actions.size());
            }
        }
        for(const auto& before:saved)if(!triggers_.Find(before.Id))ImGui::BulletText("DELETE Trigger #%llu | %s",static_cast<unsigned long long>(before.Id),before.Name.c_str());
        ImGui::TreePop();
    }
    if(ImGui::TreeNodeEx("Effects / Lights diff",ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& saved=effects_.SavedLights();
        for(const auto& current:effects_.Lights())
        {
            const auto it=std::find_if(saved.begin(),saved.end(),[&](const auto& x){return x.Id==current.Id;});
            if(it==saved.end())ImGui::BulletText("ADD Point Light #%llu",static_cast<unsigned long long>(current.Id));
            else if(!(current==*it))ImGui::BulletText("EDIT Point Light #%llu | Pos %.1f,%.1f,%.1f => %.1f,%.1f,%.1f",static_cast<unsigned long long>(current.Id),it->Position[0],it->Position[1],it->Position[2],current.Position[0],current.Position[1],current.Position[2]);
        }
        for(const auto& before:saved)if(!effects_.Find(before.Id))ImGui::BulletText("DELETE Point Light #%llu",static_cast<unsigned long long>(before.Id));
        const auto& savedRuntime=effects_.SavedRuntimeEffects();
        for(const auto& current:effects_.RuntimeEffects())
        {
            const auto it=std::find_if(savedRuntime.begin(),savedRuntime.end(),[&](const auto& x){return x.Id==current.Id;});
            if(it==savedRuntime.end())ImGui::BulletText("ADD Runtime Effect #%llu | %s",static_cast<unsigned long long>(current.Id),current.Preset.c_str());
            else if(!(current==*it))ImGui::BulletText("EDIT Runtime Effect #%llu | %s",static_cast<unsigned long long>(current.Id),current.Preset.c_str());
        }
        for(const auto& before:savedRuntime)if(!effects_.FindRuntimeEffect(before.Id))ImGui::BulletText("DELETE Runtime Effect #%llu | %s",static_cast<unsigned long long>(before.Id),before.Preset.c_str());
        ImGui::TreePop();
    }
    ImGui::Separator();
    ImGui::Text("Objects: %s",document_.Dirty()?"cambios pendientes":"sin cambios");
    ImGui::Text("Terrain: %s",(terrainAvailable_&&terrainDocument_.Dirty())?"cambios pendientes":"sin cambios");
    ImGui::Text("Effects/Lights: %s",effects_.Dirty()?"cambios pendientes":"sin cambios");
    ImGui::Text("Layers: %s",ui_.LayersDirty()?"cambios pendientes":"sin cambios");
    ImGui::Text("Gates: %s",gates_.Dirty()?"cambios pendientes":"sin cambios");
    ImGui::Text("Regions: %s",regions_.Dirty()?"cambios pendientes":"sin cambios");
    ImGui::Text("Triggers: %s",triggers_.Dirty()?"cambios pendientes":"sin cambios");
    ImGui::Separator();
    if(validationReport_)ImGui::Text("Ultimo Check Map: %zu errores | %zu advertencias",validationReport_->Errors,validationReport_->Warnings);
    else ImGui::TextDisabled("Check Map todavia no ejecutado para este estado.");
    if(ImGui::Button("Validar este estado Candidate"))
    {
        validationReport_=WorldMapValidator::Validate(document_,effects_,&monsterSpawns_,&gates_,&regions_,&triggers_,&ui_.Layers(),terrainAvailable_?&terrainDocument_:nullptr);
        changeReviewFingerprint_=reviewFingerprint;
        changeReviewValidated_=validationReport_->Errors==0;
        message_="Candidate Review validado: "+std::to_string(validationReport_->Errors)+" errores, "+std::to_string(validationReport_->Warnings)+" advertencias.";
    }
    ImGui::SameLine();
    if(changeReviewValidated_)ImGui::TextUnformatted("VALIDATED: estado actual sin errores.");
    else ImGui::TextDisabled("NOT VALIDATED para el estado actual.");
    if(validationReport_&&validationReport_->Errors>0)
    {
        ImGui::SeparatorText("Bloqueos de validacion");
        for(const auto& issue:validationReport_->Issues)if(issue.Severity==WorldValidationSeverity::Error)
            ImGui::BulletText("%s%s%llu",issue.Message.c_str(),issue.Id?" | #":"",static_cast<unsigned long long>(issue.Id));
    }
    ImGui::BeginDisabled(!changeReviewValidated_||!monsterSpawns_.Loaded());
    if(ImGui::Button("Crear backup OpenMU"))
    {
        try
        {
            const auto baselinePath=MonsterSpawnExportPath(activeMap_);
            const auto baselineBytes=ReadCandidateJson(baselinePath);
            const auto candidateBytes=monsterSpawns_.EncodeCandidate();
            const auto manifest=WorldOpenMuBackupManifest::Create(activeMap_,baselineBytes,candidateBytes,spawnChanges.size());
            const auto backupRoot=std::filesystem::path("EditorCandidates")/"Backups"/("Map"+std::to_string(activeMap_));
            std::filesystem::create_directories(backupRoot);
            const auto stamp=std::to_string(changeReviewFingerprint_);
            const auto folder=backupRoot/("validated-"+stamp);
            std::filesystem::create_directories(folder);
            const auto manifestPath=folder/"manifest.json",baselineCopy=folder/"monster-spawns.openmu.json",candidateCopy=folder/WorldMonsterSpawnDocument::CandidateFileName;
            const auto write=[&](const std::filesystem::path& path,std::span<const std::uint8_t> bytes){std::ofstream stream(path,std::ios::binary|std::ios::trunc);if(!stream)throw std::runtime_error("No se pudo crear archivo del backup OpenMU.");stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!stream)throw std::runtime_error("Backup OpenMU incompleto.");};
            write(baselineCopy,baselineBytes);write(candidateCopy,candidateBytes);const auto manifestBytes=manifest.Encode();write(manifestPath,manifestBytes);
            const auto verifyManifest=WorldOpenMuBackupManifest::Decode(ReadCandidateJson(manifestPath));
            openMuBackupVerified_=verifyManifest.Verify(ReadCandidateJson(baselineCopy),ReadCandidateJson(candidateCopy));
            openMuPreflightPassed_=false;
            if(!openMuBackupVerified_)throw std::runtime_error("Verificacion SHA del backup OpenMU fallo.");
            openMuBackupPath_=folder;
            message_="Backup OpenMU local verificado. No se modifico el servidor.";
        }
        catch(const std::exception& error){openMuBackupVerified_=false;openMuBackupPath_.clear();message_=error.what();}
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!openMuBackupVerified_);
    if(ImGui::Button("Preflight Apply"))
    {
        try
        {
            if(!candidate_||!candidate_->BaselineStillCurrent())throw std::runtime_error("Preflight bloqueado: OBJ baseline cambio desde que se abrio el mapa.");
            if(!candidateVerifyReport_||!candidateVerifyReport_->Verified())throw std::runtime_error("Preflight bloqueado: Candidate Manifest no esta VERIFIED.");
            if(terrainFiles_&&!terrainFiles_->BaselinesStillCurrent())throw std::runtime_error("Preflight bloqueado: baseline Terrain/ATT/MAP cambio desde que se abrio el mapa.");
            const auto manifestPath=openMuBackupPath_/"manifest.json";
            const auto manifest=WorldOpenMuBackupManifest::Decode(ReadCandidateJson(manifestPath));
            const auto currentBaseline=ReadCandidateJson(MonsterSpawnExportPath(activeMap_));
            const auto currentCandidate=monsterSpawns_.EncodeCandidate();
            if(manifest.MapNumber!=activeMap_)throw std::runtime_error("Preflight bloqueado: backup pertenece a otro mapa.");
            if(!manifest.BaselineMatches(currentBaseline))throw std::runtime_error("Preflight bloqueado: export OpenMU cambio desde el backup.");
            if(!manifest.CandidateMatches(currentCandidate))throw std::runtime_error("Preflight bloqueado: Candidate cambio desde el backup.");
            if(!changeReviewValidated_||reviewFingerprint!=changeReviewFingerprint_)throw std::runtime_error("Preflight bloqueado: Candidate actual no esta validado.");
            validationReport_=WorldMapValidator::Validate(document_,effects_,&monsterSpawns_,&gates_,&regions_,&triggers_,&ui_.Layers(),terrainAvailable_?&terrainDocument_:nullptr);
            if(validationReport_->Errors!=0)throw std::runtime_error("Preflight bloqueado: Check Map contiene errores.");
            const auto plan=WorldOpenMuApplyPlan::Create(monsterSpawns_,manifest.BaselineSha256,manifest.CandidateSha256);
            if(!plan.ValidateAgainst(monsterSpawns_,manifest.BaselineSha256))throw std::runtime_error("Preflight bloqueado: Apply Plan no coincide con baseline.");
            const auto planBytes=plan.Encode();const auto decodedPlan=WorldOpenMuApplyPlan::Decode(planBytes);
            if(decodedPlan.SpawnPatches!=plan.SpawnPatches)throw std::runtime_error("Preflight bloqueado: Apply Plan no supera round-trip.");
            const auto rollback=plan.Inverse();const auto rollbackBytes=rollback.Encode();
            const auto writePlan=[&](const std::filesystem::path& path,std::span<const std::uint8_t> bytes){std::ofstream stream(path,std::ios::binary|std::ios::trunc);if(!stream)throw std::runtime_error("No se pudo escribir plan OpenMU.");stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!stream)throw std::runtime_error("Plan OpenMU incompleto.");};
            writePlan(openMuBackupPath_/"apply-plan.json",planBytes);writePlan(openMuBackupPath_/"rollback-plan.json",rollbackBytes);
            openMuPreflightPassed_=true;
            message_="Preflight PASS: Apply Plan + Rollback Plan generados sin ejecutar escritura OpenMU.";
        }
        catch(const std::exception& error){openMuPreflightPassed_=false;message_=error.what();}
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(true);
    ImGui::Button("Apply OpenMU");
    ImGui::SameLine();ImGui::Button("Rollback OpenMU");
    ImGui::EndDisabled();
    if(candidate_)ImGui::Text("OBJ baseline SHA-256: %.16s... | %s",candidate_->BaselineSha256().c_str(),candidate_->BaselineStillCurrent()?"CURRENT":"STALE");
    if(terrainFiles_){ImGui::Text("Terrain baselines: %s",terrainFiles_->BaselinesStillCurrent()?"CURRENT":"STALE");if(ImGui::TreeNode("Terrain baseline SHA-256")){for(const auto& [name,sha]:terrainFiles_->BaselineFingerprints())ImGui::BulletText("%s | %.16s...",name.c_str(),sha.c_str());ImGui::TreePop();}}
    if(openMuBackupVerified_)ImGui::Text("Backup VERIFIED: %s",openMuBackupPath_.generic_string().c_str());
    if(openMuPreflightPassed_)ImGui::TextUnformatted("PREFLIGHT PASS: Apply/Rollback plans preparados; ninguna escritura fue ejecutada.");
    ImGui::BeginDisabled(!openMuPreflightPassed_);
    if(ImGui::Button("Dry Run Apply"))
    {
        try
        {
            const auto manifest=WorldOpenMuBackupManifest::Decode(ReadCandidateJson(openMuBackupPath_/"manifest.json"));
            const auto plan=WorldOpenMuApplyPlan::Decode(ReadCandidateJson(openMuBackupPath_/"apply-plan.json"));
            const auto liveBytes=ReadCandidateJson(MonsterSpawnExportPath(activeMap_));
            WorldMonsterSpawnDocument live;live.Load(liveBytes,activeMap_);
            openMuDryRun_=WorldOpenMuDryRun::Evaluate(plan,live,manifest.BaselineSha256);
            message_="Dry Run: WOULD UPDATE "+std::to_string(openMuDryRun_->WouldUpdate)+", CONFLICT "+std::to_string(openMuDryRun_->Conflicts)+", MISSING "+std::to_string(openMuDryRun_->Missing)+". Sin escritura.";
        }
        catch(const std::exception& error){openMuDryRun_.reset();message_=error.what();}
    }
    ImGui::EndDisabled();
    if(openMuDryRun_)
    {
        ImGui::SameLine();ImGui::Text("WOULD UPDATE %zu | CONFLICT %zu | MISSING %zu",openMuDryRun_->WouldUpdate,openMuDryRun_->Conflicts,openMuDryRun_->Missing);
        if(ImGui::TreeNode("Dry Run details"))
        {
            for(const auto& item:openMuDryRun_->Items)
            {
                const char* status=item.Status==WorldOpenMuDryRunStatus::WouldUpdate?"WOULD UPDATE":(item.Status==WorldOpenMuDryRunStatus::Conflict?"CONFLICT":"MISSING");
                ImGui::BulletText("%s Spawn #%llu | %s",status,static_cast<unsigned long long>(item.Id),item.Detail.c_str());
            }
            ImGui::TreePop();
        }
    }
    ImGui::TextDisabled("Apply OpenMU permanece deshabilitado. Review -> Validate -> Backup -> Preflight -> Dry Run -> Apply -> Rollback.");
    ImGui::End();
}

void WorldEditorSession::DrawGlobalSearch()
{
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift
        && ImGui::IsKeyPressed(ImGuiKey_P, false)) openGlobalSearch_ = true;
    if (openGlobalSearch_)
    {
        ImGui::OpenPopup("Buscar en WORLD");
        openGlobalSearch_ = false;
    }
    if (!ImGui::BeginPopup("Buscar en WORLD")) return;

    ImGui::SetNextItemWidth(430.0f);
    ImGui::InputTextWithHint("##world-search", "Objeto, monster, NPC, gate, region, trigger...", globalSearch_, sizeof(globalSearch_));
    const auto matches = [this](const std::string& text)
    {
        std::string left=text, right=globalSearch_;
        std::transform(left.begin(),left.end(),left.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        std::transform(right.begin(),right.end(),right.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        return right.empty() || left.find(right)!=std::string::npos;
    };

    int shown=0;
    for(const auto& entry:document_.Entries())
    {
        if(!entry.Record || entry.Record->BaselineOnly) continue;
        const std::string label="Object #"+std::to_string(entry.Id)+" | Type "+std::to_string(entry.Record->Type);
        if(matches(label) && ImGui::Selectable(label.c_str()))
        { tool_=activeTool_=0; ui_.SelectOnly(entry.Id); ImGui::CloseCurrentPopup(); }
        if(matches(label) && ++shown>=80) break;
    }
    for(const auto& spawn:monsterSpawns_.Entries())
    {
        const std::string label=spawn.ObjectKind+" #"+std::to_string(spawn.Id)+" | "+spawn.Designation
            +" ["+std::to_string(spawn.MonsterNumber)+"]";
        if(matches(label) && ImGui::Selectable(label.c_str()))
        { selectedSpawn_=spawn.Id; tool_=activeTool_=5; ImGui::CloseCurrentPopup(); }
    }
    for(const auto& gate:gates_.Entries())
    {
        const std::string label="Gate #"+std::to_string(gate.Id)+" | "+std::to_string(gate.Number);
        if(matches(label) && ImGui::Selectable(label.c_str()))
        { selectedGate_=gate.Id; tool_=activeTool_=6; ImGui::CloseCurrentPopup(); }
    }
    for(const auto& region:regions_.Entries())
    {
        const std::string label="Region #"+std::to_string(region.Id)+" | "+region.Name+" | "+region.Tag;
        if(matches(label) && ImGui::Selectable(label.c_str()))
        { selectedRegion_=region.Id; tool_=activeTool_=7; ImGui::CloseCurrentPopup(); }
    }
    for(const auto& trigger:triggers_.Entries())
    {
        const std::string label="Trigger #"+std::to_string(trigger.Id)+" | "+trigger.Name+" | "+trigger.Event;
        if(matches(label) && ImGui::Selectable(label.c_str()))
        { selectedTrigger_=trigger.Id; tool_=activeTool_=8; ImGui::CloseCurrentPopup(); }
    }
    ImGui::EndPopup();
}

void WorldEditorSession::DrawGameplayOverlays()
{
    if (!editing_ || !scene_.Attached()) return;
    const auto camera = scene_.Camera();
    auto* draw = ImGui::GetForegroundDrawList();
    const auto project = [&camera](float x, float y, std::array<float,2>& out)
    {
        return camera.Project({x, y, RequestTerrainHeight(x, y) + 18.0f}, out);
    };
    const auto line = [draw, &project](float x1,float y1,float x2,float y2,ImU32 color,float width)
    {
        std::array<float,2> a{},b{};
        if(project(x1,y1,a) && project(x2,y2,b)) draw->AddLine({a[0],a[1]},{b[0],b[1]},color,width);
    };
    const auto rect = [&line](float x1,float y1,float x2,float y2,ImU32 color,float width)
    {
        line(x1,y1,x2,y1,color,width); line(x2,y1,x2,y2,color,width);
        line(x2,y2,x1,y2,color,width); line(x1,y2,x1,y1,color,width);
    };
    if(showViewportGrid_)
    {
        constexpr float Grid=1000.0f,World=25600.0f;
        const ImU32 gridColor=IM_COL32(145,165,180,42);
        for(float axis=0.0f;axis<=World;axis+=Grid)
        {
            for(float step=0.0f;step<World;step+=Grid)
            {
                line(axis,step,axis,std::min(step+Grid,World),gridColor,1.0f);
                line(step,axis,std::min(step+Grid,World),axis,gridColor,1.0f);
            }
        }
    }
    for(const auto& region : regions_.Entries())
    {
        const bool selected=selectedRegion_==region.Id;
        const ImU32 color=selected?IM_COL32(255,220,80,245):IM_COL32(80,210,255,185);
        if(region.Shape==WorldRegionShape::Rectangle)
            rect(region.X1,region.Y1,region.X2,region.Y2,color,selected?3.0f:1.5f);
        else
        {
            constexpr int Segments=32;
            constexpr float Tau=6.28318530718f;
            for(int i=0;i<Segments;++i)
            {
                const float a=Tau*i/Segments,b=Tau*(i+1)/Segments;
                line(region.X1+std::cos(a)*region.Radius,region.Y1+std::sin(a)*region.Radius,
                    region.X1+std::cos(b)*region.Radius,region.Y1+std::sin(b)*region.Radius,
                    color,selected?3.0f:1.5f);
            }
        }
    }
    constexpr float Cell=100.0f;
    if(monsterSpawns_.Loaded()) for(const auto& spawn:monsterSpawns_.Entries())
    {
        const bool selected=selectedSpawn_==spawn.Id;
        const bool npc=spawn.ObjectKind=="PassiveNpc"||spawn.ObjectKind=="Npc"||spawn.ObjectKind=="NPC";
        const bool guard=spawn.ObjectKind=="Guard";
        const ImU32 baseColor=guard?IM_COL32(100,210,255,180):(npc?IM_COL32(190,120,255,180):IM_COL32(255,110,90,165));
        const ImU32 color=selected?IM_COL32(255,220,70,250):baseColor;
        const float x1=spawn.X1*Cell,y1=spawn.Y1*Cell,x2=(spawn.X2+1.0f)*Cell,y2=(spawn.Y2+1.0f)*Cell;
        rect(x1,y1,x2,y2,color,selected?3.0f:1.2f);
        if(selected)
        {
            const float cx=(x1+x2)*0.5f,cy=(y1+y2)*0.5f;
            constexpr float Pi=3.14159265359f;
            const float angle=(static_cast<float>(spawn.Direction ? spawn.Direction-1 : 0)*45.0f-90.0f)*Pi/180.0f;
            const float length=std::max(120.0f,std::min(x2-x1,y2-y1)*0.35f);
            const float ex=cx+std::cos(angle)*length,ey=cy+std::sin(angle)*length;
            line(cx,cy,ex,ey,IM_COL32(255,245,150,255),3.0f);
            std::array<float,2> screen{};
            if(project(cx,cy,screen))
            {
                const std::string label=spawn.ObjectKind+" | #"+std::to_string(spawn.MonsterNumber)+" "+spawn.Designation
                    +" | Qty "+std::to_string(spawn.Quantity)+" | Dir "+std::to_string(spawn.Direction);
                draw->AddRectFilled({screen[0]-4.0f,screen[1]-24.0f},{screen[0]+static_cast<float>(label.size())*7.0f+8.0f,screen[1]-5.0f},IM_COL32(15,20,25,205),3.0f);
                draw->AddText({screen[0]+2.0f,screen[1]-22.0f},IM_COL32(245,245,245,255),label.c_str());
            }
        }
    }
    if(gates_.Loaded()) for(const auto& gate:gates_.Entries())
    {
        const bool selected=selectedGate_==gate.Id;
        const float x1=gate.X1*Cell,y1=gate.Y1*Cell,x2=(gate.X2+1.0f)*Cell,y2=(gate.Y2+1.0f)*Cell;
        rect(x1,y1,x2,y2,selected?IM_COL32(255,240,90,245):IM_COL32(120,255,140,170),selected?3.0f:1.2f);
        if(selected)
        {
            const float cx=(x1+x2)*0.5f,cy=(y1+y2)*0.5f;std::array<float,2> screen{};
            if(project(cx,cy,screen))
            {
                const std::string label="Gate "+std::to_string(gate.Number)+" | Lv "+std::to_string(gate.LevelRequirement)
                    +" -> Map "+std::to_string(gate.TargetMap)+" / Gate "+std::to_string(gate.TargetGateId);
                draw->AddRectFilled({screen[0]-4.0f,screen[1]-24.0f},{screen[0]+static_cast<float>(label.size())*7.0f+8.0f,screen[1]-5.0f},IM_COL32(15,25,20,210),3.0f);
                draw->AddText({screen[0]+2.0f,screen[1]-22.0f},IM_COL32(225,255,225,255),label.c_str());
            }
            if(gate.TargetMap==activeMap_)
            {
                const float tx=(gate.TargetX1+gate.TargetX2+1.0f)*0.5f*Cell,ty=(gate.TargetY1+gate.TargetY2+1.0f)*0.5f*Cell;
                line(cx,cy,tx,ty,IM_COL32(140,255,180,220),2.0f);
                rect(gate.TargetX1*Cell,gate.TargetY1*Cell,(gate.TargetX2+1.0f)*Cell,(gate.TargetY2+1.0f)*Cell,IM_COL32(100,255,180,180),2.0f);
            }
        }
    }
}

void WorldEditorSession::HandleRegionViewportEdit()
{
    if (tool_ != 7 || selectedRegion_ == NoRegion || !scene_.Attached()) return;
    const auto* selected = regions_.Find(selectedRegion_);
    if (!selected) { selectedRegion_ = NoRegion; regionDragHandle_ = -1; return; }

    const auto camera = scene_.Camera();
    auto* draw = ImGui::GetForegroundDrawList();
    const auto project = [&camera](float x, float y, std::array<float,2>& out)
    {
        return camera.Project({x, y, RequestTerrainHeight(x, y) + 24.0f}, out);
    };
    struct Handle { int Id; float X; float Y; };
    std::vector<Handle> handles;
    if (selected->Shape == WorldRegionShape::Rectangle)
    {
        handles = {
            {0,(selected->X1+selected->X2)*0.5f,(selected->Y1+selected->Y2)*0.5f},
            {1,selected->X1,selected->Y1},{2,selected->X2,selected->Y1},
            {3,selected->X2,selected->Y2},{4,selected->X1,selected->Y2}
        };
    }
    else
    {
        handles = {{0,selected->X1,selected->Y1},{5,selected->X1+selected->Radius,selected->Y1}};
    }

    const auto mouse = ImGui::GetIO().MousePos;
    int hovered = -1;
    float best = 12.0f * 12.0f;
    for (const auto& handle : handles)
    {
        std::array<float,2> screen{};
        if (!project(handle.X,handle.Y,screen)) continue;
        const float dx=mouse.x-screen[0], dy=mouse.y-screen[1], distance=dx*dx+dy*dy;
        if (distance <= best) { best=distance; hovered=handle.Id; }
        const bool active = regionDragHandle_ == handle.Id;
        draw->AddCircleFilled({screen[0],screen[1]}, active ? 7.0f : 5.0f,
            active ? IM_COL32(255,150,60,255) : IM_COL32(255,235,100,245));
        draw->AddCircle({screen[0],screen[1]}, active ? 9.0f : 7.0f, IM_COL32(20,25,30,245), 0, 2.0f);
    }

    const bool viewportMouse = !ImGui::GetIO().WantCaptureMouse
        && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    if (regionDragHandle_ < 0 && viewportMouse && hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        std::array<float,3> world{};
        if (scene_.Placement(mouse.x,mouse.y,world))
        {
            try
            {
                regionDragHandle_=hovered;
                regionDragOrigin_=*selected;
                regionDragStartWorld_=world;
                regions_.BeginEdit(selectedRegion_, hovered==0 ? "Move Region" : "Resize Region");
                message_ = hovered==0 ? "Moviendo Region en viewport..." : "Redimensionando Region en viewport...";
            }
            catch (const std::exception& error) { message_=error.what(); regionDragHandle_=-1; }
        }
    }

    if (regionDragHandle_ >= 0)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            regions_.CancelEdit();
            regionDragHandle_=-1;
            if (const auto* current=regions_.Find(selectedRegion_)) inspectorRegionEdit_=*current;
            message_="Edicion de Region cancelada.";
            return;
        }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            std::array<float,3> world{};
            if (scene_.Placement(mouse.x,mouse.y,world))
            {
                WorldRegion changed=regionDragOrigin_;
                if (regionDragHandle_==0)
                {
                    const float dx=world[0]-regionDragStartWorld_[0], dy=world[1]-regionDragStartWorld_[1];
                    changed.X1+=dx; changed.Y1+=dy; changed.X2+=dx; changed.Y2+=dy;
                }
                else if (changed.Shape==WorldRegionShape::Circle)
                {
                    changed.Radius=std::max(1.0f,std::hypot(world[0]-changed.X1,world[1]-changed.Y1));
                }
                else
                {
                    float oppositeX=0.0f, oppositeY=0.0f;
                    if(regionDragHandle_==1){oppositeX=regionDragOrigin_.X2;oppositeY=regionDragOrigin_.Y2;}
                    if(regionDragHandle_==2){oppositeX=regionDragOrigin_.X1;oppositeY=regionDragOrigin_.Y2;}
                    if(regionDragHandle_==3){oppositeX=regionDragOrigin_.X1;oppositeY=regionDragOrigin_.Y1;}
                    if(regionDragHandle_==4){oppositeX=regionDragOrigin_.X2;oppositeY=regionDragOrigin_.Y1;}
                    changed.X1=std::min(world[0],oppositeX); changed.X2=std::max(world[0],oppositeX);
                    changed.Y1=std::min(world[1],oppositeY); changed.Y2=std::max(world[1],oppositeY);
                }
                try
                {
                    regions_.Preview(changed);
                    inspectorRegionEdit_=changed;
                }
                catch (const std::exception& error) { message_=error.what(); }
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            try
            {
                const bool committed=regions_.CommitEdit();
                message_=committed ? "Region actualizada desde viewport; una sola transaccion en History global."
                    : "Region sin cambios.";
            }
            catch (const std::exception& error) { regions_.CancelEdit(); message_=error.what(); }
            regionDragHandle_=-1;
            if (const auto* current=regions_.Find(selectedRegion_)) inspectorRegionEdit_=*current;
        }
    }
}


void WorldEditorSession::HandleSpawnViewportEdit()
{
    if(npcLibraryUi_.Placing()){spawnDragHandle_=-1;return;}
    if(tool_!=5||selectedSpawn_==NoSpawn||!scene_.Attached()||!monsterSpawns_.Loaded())return;
    const auto* selected=monsterSpawns_.Find(selectedSpawn_);
    if(!selected){selectedSpawn_=NoSpawn;spawnDragHandle_=-1;return;}
    constexpr float Cell=100.0f;
    const auto camera=scene_.Camera();auto* draw=ImGui::GetForegroundDrawList();
    const auto project=[&camera](float x,float y,std::array<float,2>& out){return camera.Project({x,y,RequestTerrainHeight(x,y)+28.0f},out);};
    struct Handle{int Id;float X;float Y;};
    const float x1=selected->X1*Cell,y1=selected->Y1*Cell,x2=(selected->X2+1.0f)*Cell,y2=(selected->Y2+1.0f)*Cell;
    const std::array<Handle,5> handles{{{0,(x1+x2)*0.5f,(y1+y2)*0.5f},{1,x1,y1},{2,x2,y1},{3,x2,y2},{4,x1,y2}}};
    const auto mouse=ImGui::GetIO().MousePos;int hovered=-1;float best=12.0f*12.0f;
    for(const auto& handle:handles)
    {
        std::array<float,2> screen{};if(!project(handle.X,handle.Y,screen))continue;
        const float dx=mouse.x-screen[0],dy=mouse.y-screen[1],distance=dx*dx+dy*dy;if(distance<=best){best=distance;hovered=handle.Id;}
        const bool active=spawnDragHandle_==handle.Id;
        draw->AddRectFilled({screen[0]-(active?7.0f:5.0f),screen[1]-(active?7.0f:5.0f)},{screen[0]+(active?7.0f:5.0f),screen[1]+(active?7.0f:5.0f)},active?IM_COL32(255,210,80,255):IM_COL32(255,125,95,245),2.0f);
    }
    const bool viewportMouse=!ImGui::GetIO().WantCaptureMouse&&!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    if(spawnDragHandle_<0&&viewportMouse&&hovered>=0&&ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        std::array<float,3> world{};if(scene_.Placement(mouse.x,mouse.y,world)){spawnDragHandle_=hovered;spawnDragOrigin_=*selected;spawnDragStartWorld_=world;message_=hovered==0?"Moviendo MonsterSpawn Candidate...":"Redimensionando MonsterSpawn Candidate...";}
    }
    if(spawnDragHandle_<0)return;
    if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)){spawnDragHandle_=-1;inspectorSpawnEdit_=spawnDragOrigin_;message_="Edicion viewport de MonsterSpawn cancelada.";return;}
    if(ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        std::array<float,3> world{};if(scene_.Placement(mouse.x,mouse.y,world))
        {
            WorldMonsterSpawn changed=spawnDragOrigin_;
            auto cell=[](float value){return std::clamp(static_cast<int>(std::floor(value/Cell)),0,255);};
            if(spawnDragHandle_==0)
            {
                const int dx=cell(world[0])-cell(spawnDragStartWorld_[0]),dy=cell(world[1])-cell(spawnDragStartWorld_[1]);
                const int width=spawnDragOrigin_.X2-spawnDragOrigin_.X1,height=spawnDragOrigin_.Y2-spawnDragOrigin_.Y1;
                const int nx=std::clamp(static_cast<int>(spawnDragOrigin_.X1)+dx,0,255-width),ny=std::clamp(static_cast<int>(spawnDragOrigin_.Y1)+dy,0,255-height);
                changed.X1=static_cast<std::uint8_t>(nx);changed.Y1=static_cast<std::uint8_t>(ny);changed.X2=static_cast<std::uint8_t>(nx+width);changed.Y2=static_cast<std::uint8_t>(ny+height);
            }
            else
            {
                const int cx=cell(world[0]),cy=cell(world[1]);
                int ox=0,oy=0;
                if(spawnDragHandle_==1){ox=spawnDragOrigin_.X2;oy=spawnDragOrigin_.Y2;}
                if(spawnDragHandle_==2){ox=spawnDragOrigin_.X1;oy=spawnDragOrigin_.Y2;}
                if(spawnDragHandle_==3){ox=spawnDragOrigin_.X1;oy=spawnDragOrigin_.Y1;}
                if(spawnDragHandle_==4){ox=spawnDragOrigin_.X2;oy=spawnDragOrigin_.Y1;}
                changed.X1=static_cast<std::uint8_t>(std::min(cx,ox));changed.X2=static_cast<std::uint8_t>(std::max(cx,ox));
                changed.Y1=static_cast<std::uint8_t>(std::min(cy,oy));changed.Y2=static_cast<std::uint8_t>(std::max(cy,oy));
            }
            inspectorSpawnEdit_=changed;
        }
    }
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        try
        {
            const bool changed=monsterSpawns_.SetCandidate(selectedSpawn_,inspectorSpawnEdit_,spawnDragHandle_==0?"Move Monster Spawn":"Resize Monster Spawn");
            message_=changed?"MonsterSpawn actualizado desde viewport con snap exacto a celdas MU.":"MonsterSpawn sin cambios.";
        }
        catch(const std::exception& error){message_=error.what();inspectorSpawnEdit_=spawnDragOrigin_;}
        spawnDragHandle_=-1;if(const auto* current=monsterSpawns_.Find(selectedSpawn_))inspectorSpawnEdit_=*current;
    }
}

void WorldEditorSession::DrawUniversalInspector()
{
    if (!ImGui::CollapsingHeader("Inspector universal", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (tool_ == 0 && ui_.Selected() != NoObject)
    {
        const ObjectId id = ui_.Selected();
        const auto* current = document_.Find(id);
        if (!current)
        {
            inspectorObject_ = NoObject;
            ImGui::TextDisabled("El objeto seleccionado ya no existe.");
            return;
        }
        if (inspectorObject_ != id)
        {
            inspectorObject_ = id;
            inspectorObjectEdit_ = *current;
        }

        const auto layerId = ui_.Layers().LayerFor(id);
        const auto* layer = ui_.Layers().Find(layerId);
        ImGui::Text("Object #%u | Type/Model %d", id, current->Type);
        ImGui::Text("Layer: %s | Flags: %s%s", layer ? layer->Name.c_str() : "Base",
            current->BaselineOnly ? "BaselineOnly " : "",
            ui_.Layers().IsLocked(id) ? "Locked" : "Editable");
        ImGui::InputFloat3("Position XYZ##universal-object", inspectorObjectEdit_.Position.data(), "%.2f");
        ImGui::InputFloat3("Rotation XYZ##universal-object", inspectorObjectEdit_.Angle.data(), "%.2f");
        ImGui::DragFloat("Scale##universal-object", &inspectorObjectEdit_.Scale, 0.01f, 0.01f, 100.0f, "%.3f");

        const bool editable = ui_.Layers().IsVisible(id) && !ui_.Layers().IsLocked(id)
            && !current->BaselineOnly && !document_.HasPreview();
        ImGui::BeginDisabled(!editable);
        if (ImGui::Button("Aplicar objeto"))
        {
            try
            {
                if (document_.Set(id, inspectorObjectEdit_, "Inspector universal: objeto"))
                    message_ = "Inspector universal aplico cambios al objeto #" + std::to_string(id) + ".";
                inspectorObjectEdit_ = *document_.Find(id);
            }
            catch (const std::exception& error) { message_ = error.what(); }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Recargar objeto")) inspectorObjectEdit_ = *current;
        return;
    }

    if (tool_ == 3 && effectsUi_.Selected() != NoEffect)
    {
        const EffectId id = effectsUi_.Selected();
        const auto* current = effects_.Find(id);
        if (!current)
        {
            inspectorLight_ = NoEffect;
            ImGui::TextDisabled("La luz seleccionada ya no existe.");
            return;
        }
        if (inspectorLight_ != id)
        {
            inspectorLight_ = id;
            inspectorLightEdit_ = *current;
        }

        ImGui::Text("Point Light #%llu", static_cast<unsigned long long>(id));
        ImGui::InputFloat3("Position XYZ##universal-light", inspectorLightEdit_.Position.data(), "%.2f");
        ImGui::ColorEdit3("Color##universal-light", inspectorLightEdit_.Color.data());
        ImGui::SliderFloat("Intensity##universal-light", &inspectorLightEdit_.Intensity, 0.0f, 4.0f, "%.2f");
        ImGui::SliderInt("Radius##universal-light", &inspectorLightEdit_.Radius, 1, 32);
        if (ImGui::Button("Aplicar luz"))
        {
            try
            {
                if (effects_.UpdatePointLight(id, inspectorLightEdit_))
                    message_ = "Inspector universal aplico cambios a Point Light #" + std::to_string(id) + ".";
                effectsUi_.SelectById(effects_, id);
                if (const auto* updated = effects_.Find(id)) inspectorLightEdit_ = *updated;
            }
            catch (const std::exception& error) { message_ = error.what(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Recargar luz")) inspectorLightEdit_ = *current;
        return;
    }

    if (tool_ == 5 && selectedSpawn_ != NoSpawn)
    {
        const auto* spawn = monsterSpawns_.Find(selectedSpawn_);
        if (!spawn)
        {
            selectedSpawn_ = NoSpawn;
            ImGui::TextDisabled("El MonsterSpawn seleccionado ya no existe.");
            return;
        }

        if(inspectorSpawn_!=spawn->Id){inspectorSpawn_=spawn->Id;inspectorSpawnEdit_=*spawn;}
        ImGui::Text("%s #%llu", spawn->ObjectKind.c_str(),static_cast<unsigned long long>(spawn->Id));
        ImGui::Text("Monster ID: %d | %s", spawn->MonsterNumber, spawn->Designation.c_str());
        int bounds[4]{inspectorSpawnEdit_.X1,inspectorSpawnEdit_.Y1,inspectorSpawnEdit_.X2,inspectorSpawnEdit_.Y2};
        if(ImGui::InputInt4("Area X1 Y1 X2 Y2##spawn",bounds))
        {
            for(auto& v:bounds)v=std::clamp(v,0,255);
            inspectorSpawnEdit_.X1=static_cast<std::uint8_t>(bounds[0]);inspectorSpawnEdit_.Y1=static_cast<std::uint8_t>(bounds[1]);
            inspectorSpawnEdit_.X2=static_cast<std::uint8_t>(bounds[2]);inspectorSpawnEdit_.Y2=static_cast<std::uint8_t>(bounds[3]);
        }
        int quantity=inspectorSpawnEdit_.Quantity,direction=inspectorSpawnEdit_.Direction;
        if(ImGui::InputInt("Quantity##spawn",&quantity))inspectorSpawnEdit_.Quantity=static_cast<std::int16_t>(std::clamp(quantity,0,32767));
        if(ImGui::SliderInt("Direction##spawn",&direction,0,8))inspectorSpawnEdit_.Direction=static_cast<std::uint8_t>(direction);
        ImGui::Text("Trigger: %s | Wave: %u",spawn->SpawnTrigger.c_str(),spawn->WaveNumber);
        ImGui::Text("Respawn: %lld ms",static_cast<long long>(spawn->RespawnDelayMilliseconds));
        if(ImGui::Button("Aplicar Spawn Candidate"))
        {
            try{if(monsterSpawns_.SetCandidate(spawn->Id,inspectorSpawnEdit_,"Edit Monster Spawn"))message_="MonsterSpawn editado solo en Candidate; OpenMU permanece read-only.";if(const auto* current=monsterSpawns_.Find(selectedSpawn_))inspectorSpawnEdit_=*current;}
            catch(const std::exception& error){message_=error.what();if(const auto* current=monsterSpawns_.Find(selectedSpawn_))inspectorSpawnEdit_=*current;}
        }
        ImGui::SameLine();if(ImGui::Button("Recargar Spawn"))inspectorSpawnEdit_=*spawn;
        ImGui::TextDisabled("Read-only: fuente actual OpenMU. Los cambios del editor se guardan aparte en monster-spawns.candidate.json.");
        ImGui::TextDisabled("Apply al servidor sigue deshabilitado hasta disponer de validacion + rollback.");
        return;
    }

    if (tool_ == 6 && selectedGate_ != NoGate)
    {
        gateUi_.Draw(gates_,selectedGate_);
        return;
    }

    if (tool_ == 7 && selectedRegion_ != NoRegion)
    {
        const auto* region = regions_.Find(selectedRegion_);
        if (!region)
        {
            selectedRegion_ = NoRegion;
            inspectorRegion_ = NoRegion;
            ImGui::TextDisabled("La Region seleccionada ya no existe.");
            return;
        }
        if (inspectorRegion_ != region->Id)
        {
            inspectorRegion_ = region->Id;
            inspectorRegionEdit_ = *region;
        }
        ImGui::Text("Region #%llu | %s", static_cast<unsigned long long>(region->Id), region->Name.c_str());
        ImGui::Text("Shape: %s | Tag: %s",
            region->Shape == WorldRegionShape::Rectangle ? "Rectangle" : "Circle",
            region->Tag.empty() ? "(sin tag)" : region->Tag.c_str());
        if (region->Shape == WorldRegionShape::Rectangle)
        {
            float minimum[2]{inspectorRegionEdit_.X1, inspectorRegionEdit_.Y1};
            float maximum[2]{inspectorRegionEdit_.X2, inspectorRegionEdit_.Y2};
            if (ImGui::InputFloat2("Min XY##region", minimum, "%.1f"))
            { inspectorRegionEdit_.X1=minimum[0]; inspectorRegionEdit_.Y1=minimum[1]; }
            if (ImGui::InputFloat2("Max XY##region", maximum, "%.1f"))
            { inspectorRegionEdit_.X2=maximum[0]; inspectorRegionEdit_.Y2=maximum[1]; }
        }
        else
        {
            float center[2]{inspectorRegionEdit_.X1, inspectorRegionEdit_.Y1};
            if (ImGui::InputFloat2("Center XY##region", center, "%.1f"))
            { inspectorRegionEdit_.X1=center[0]; inspectorRegionEdit_.Y1=center[1]; inspectorRegionEdit_.X2=center[0]; inspectorRegionEdit_.Y2=center[1]; }
            ImGui::DragFloat("Radius##region", &inspectorRegionEdit_.Radius, 10.0f, 1.0f, 25600.0f, "%.1f");
        }
        if (ImGui::Button("Aplicar Region"))
        {
            try
            {
                if (regions_.Set(region->Id, inspectorRegionEdit_, "Inspector: Edit Region"))
                    message_ = "Region actualizada en Candidate; History global registra la operacion.";
                if (const auto* updated=regions_.Find(selectedRegion_)) inspectorRegionEdit_=*updated;
            }
            catch (const std::exception& error)
            {
                message_ = error.what();
                if (const auto* current=regions_.Find(selectedRegion_)) inspectorRegionEdit_=*current;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Recargar Region")) inspectorRegionEdit_ = *region;
        if (ImGui::Button("Eliminar Region"))
        {
            regions_.Remove(region->Id);
            selectedRegion_ = NoRegion;
            inspectorRegion_ = NoRegion;
            message_ = "Region retirada del Candidate. Data original intacto.";
        }
        ImGui::TextDisabled("Edicion Candidate. Undo/Redo participa del History global.");
        return;
    }

    if (tool_ == 8 && selectedTrigger_ != NoTrigger)
    {
        const auto* trigger = triggers_.Find(selectedTrigger_);
        if (!trigger)
        {
            selectedTrigger_ = NoTrigger;
            ImGui::TextDisabled("El Trigger seleccionado ya no existe.");
            return;
        }
        if (inspectorTrigger_ != trigger->Id)
        {
            inspectorTrigger_ = trigger->Id;
            inspectorTriggerEdit_ = *trigger;
        }
        ImGui::Text("Trigger #%llu | %s", static_cast<unsigned long long>(trigger->Id), trigger->Name.c_str());
        ImGui::Text("Event: %s | Region: %llu | %s", trigger->Event.c_str(),
            static_cast<unsigned long long>(trigger->Region), trigger->Enabled ? "Enabled" : "Disabled");
        char triggerNameEdit[161]{}, triggerEventEdit[161]{};
        std::snprintf(triggerNameEdit, sizeof(triggerNameEdit), "%s", inspectorTriggerEdit_.Name.c_str());
        std::snprintf(triggerEventEdit, sizeof(triggerEventEdit), "%s", inspectorTriggerEdit_.Event.c_str());
        if (ImGui::InputText("Name##trigger-edit", triggerNameEdit, sizeof(triggerNameEdit))) inspectorTriggerEdit_.Name = triggerNameEdit;
        if (ImGui::InputText("Event##trigger-edit", triggerEventEdit, sizeof(triggerEventEdit))) inspectorTriggerEdit_.Event = triggerEventEdit;
        ImGui::Checkbox("Enabled##trigger-edit", &inspectorTriggerEdit_.Enabled);
        int regionChoice = -1;
        for (std::size_t i=0;i<regions_.Entries().size();++i) if(regions_.Entries()[i].Id==inspectorTriggerEdit_.Region) regionChoice=static_cast<int>(i);
        const char* regionPreview = regionChoice>=0 ? regions_.Entries()[static_cast<std::size_t>(regionChoice)].Name.c_str() : "(sin region)";
        if(ImGui::BeginCombo("Region##trigger-edit",regionPreview))
        {
            if(ImGui::Selectable("(sin region)",regionChoice<0)){inspectorTriggerEdit_.Region=NoRegion;regionChoice=-1;}
            for(std::size_t i=0;i<regions_.Entries().size();++i)
                if(ImGui::Selectable(regions_.Entries()[i].Name.c_str(),regionChoice==static_cast<int>(i)))
                {inspectorTriggerEdit_.Region=regions_.Entries()[i].Id;regionChoice=static_cast<int>(i);}
            ImGui::EndCombo();
        }
        if(ImGui::Button("Aplicar Trigger"))
        {
            try
            {
                if(triggers_.Set(trigger->Id,inspectorTriggerEdit_,"Edit Trigger Header"))
                    message_="Trigger actualizado en Candidate; History global registra la operacion.";
                if(const auto* current=triggers_.Find(selectedTrigger_)) inspectorTriggerEdit_=*current;
            }
            catch(const std::exception& error){message_=error.what();if(const auto* current=triggers_.Find(selectedTrigger_))inspectorTriggerEdit_=*current;}
        }
        ImGui::SameLine();
        if(ImGui::Button("Recargar Trigger")) inspectorTriggerEdit_=*trigger;
        const auto graphResult=triggerFlowUi_.Draw(triggers_,trigger->Id,regions_);
        if(graphResult.Changed)inspectorTriggerEdit_=*triggers_.Find(trigger->Id);
        if(graphResult.FocusRegion!=NoRegion)
        {selectedRegion_=graphResult.FocusRegion;tool_=activeTool_=7;}

        ImGui::SeparatorText("Conditions");
        for (std::size_t index=0; index<trigger->Conditions.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            ImGui::Text("%zu. %s",index+1,trigger->Conditions[index].c_str());
            ImGui::SameLine(); ImGui::BeginDisabled(index==0);
            if(ImGui::SmallButton("Up")){triggers_.MoveCondition(trigger->Id,index,-1);inspectorTriggerEdit_=*triggers_.Find(trigger->Id);}
            ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(index+1>=trigger->Conditions.size());
            if(ImGui::SmallButton("Down")){triggers_.MoveCondition(trigger->Id,index,1);inspectorTriggerEdit_=*triggers_.Find(trigger->Id);}
            ImGui::EndDisabled(); ImGui::SameLine();
            if(ImGui::SmallButton("Remove")){triggers_.RemoveCondition(trigger->Id,index);inspectorTriggerEdit_=*triggers_.Find(trigger->Id);ImGui::PopID();break;}
            ImGui::PopID();
        }
        ImGui::InputTextWithHint("##trigger-condition", "Condition...", triggerCondition_, sizeof(triggerCondition_));
        if (ImGui::Button("Agregar Condition") && triggerCondition_[0] != 0)
        {
            triggers_.AddCondition(trigger->Id, triggerCondition_);
            triggerCondition_[0] = 0;
        }
        ImGui::SeparatorText("Actions");
        for (std::size_t index=0; index<trigger->Actions.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(1000+index));
            ImGui::Text("%zu. %s",index+1,trigger->Actions[index].c_str());
            ImGui::SameLine(); ImGui::BeginDisabled(index==0);
            if(ImGui::SmallButton("Up")){triggers_.MoveAction(trigger->Id,index,-1);inspectorTriggerEdit_=*triggers_.Find(trigger->Id);}
            ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(index+1>=trigger->Actions.size());
            if(ImGui::SmallButton("Down")){triggers_.MoveAction(trigger->Id,index,1);inspectorTriggerEdit_=*triggers_.Find(trigger->Id);}
            ImGui::EndDisabled(); ImGui::SameLine();
            if(ImGui::SmallButton("Remove")){triggers_.RemoveAction(trigger->Id,index);inspectorTriggerEdit_=*triggers_.Find(trigger->Id);ImGui::PopID();break;}
            ImGui::PopID();
        }
        ImGui::InputTextWithHint("##trigger-action", "Action...", triggerAction_, sizeof(triggerAction_));
        if (ImGui::Button("Agregar Action") && triggerAction_[0] != 0)
        {
            triggers_.AddAction(trigger->Id, triggerAction_);
            triggerAction_[0] = 0;
        }
        if (ImGui::Button("Eliminar Trigger"))
        {
            triggers_.Remove(trigger->Id);
            selectedTrigger_ = NoTrigger;
            inspectorTrigger_ = NoTrigger;
        }
        ImGui::TextDisabled("Candidate-only: todavia no ejecuta logica en OpenMU.");
        ImGui::TextDisabled("Edicion y orden de nodos participan del History global.");
        return;
    }

    if (tool_ == 1)
    {
        ImGui::Text("Terrain | Map %d | World %d", activeMap_, activeWorldNumber_);
        ImGui::TextDisabled("Propiedades de celda/pincel viven en Terrain hasta que exista seleccion de entidad Terrain tipada.");
        return;
    }

    ImGui::TextDisabled("Selecciona Objects, Terrain o Lights/Effects en WORLD para inspeccionar propiedades reales.");
}

void WorldEditorSession::DrawPerformanceAnalyzer()
{
    if (!ImGui::CollapsingHeader("Performance Analyzer"))
        return;

    bool enabled = performanceStatsEnabled_;
    if (ImGui::Checkbox("Capturar estadisticas del renderer", &enabled))
    {
        performanceStatsEnabled_ = enabled;
        mu::GetRenderer().SetStatsEnabled(enabled);
    }
    if (!performanceStatsEnabled_)
    {
        ImGui::TextDisabled("Desactivado: evita costo de instrumentacion cuando no se necesita.");
        return;
    }

    const auto stats = mu::GetRenderer().GetFrameStats();
    const double fps = stats.frameMilliseconds > 0.0 ? 1000.0 / stats.frameMilliseconds : 0.0;
    ImGui::Text("Frame %.2f ms | %.1f FPS", stats.frameMilliseconds, fps);
    ImGui::Text("Draw calls: requested %u | submitted %u | merged %u (+2D %u)",
        stats.requestedDrawCalls, stats.submittedDrawCalls, stats.mergedDrawCalls, stats.merged2DDrawCalls);
    ImGui::Text("Commands %u | vertex stream %.2f MiB",
        stats.commandCount, static_cast<double>(stats.vertexBytes) / (1024.0 * 1024.0));
    ImGui::Text("Replay %.2f ms | submit %.2f ms", stats.replayMilliseconds, stats.submitMilliseconds);
    ImGui::Text("Textures: uploads %u | creates %u | releases %u",
        stats.textureUploads, stats.textureCreates, stats.textureReleases);
    ImGui::Text("Bindings: pipeline %u | sampler %u | uniforms V/F %u/%u",
        stats.pipelineBinds, stats.samplerBinds, stats.vertexUniformPushes, stats.fragmentUniformPushes);
    ImGui::TextDisabled("Lectura del backend activo; no modifica calidad, assets ni Data.");
}

void WorldEditorSession::CaptureGlobalHistory()
{
    std::vector<WorldHistoryDomain> changed;
    std::string label;

    const auto objectSerial = document_.HistorySerial();
    const auto terrainSerial = terrainDocument_.HistorySerial();
    const auto effectSerial = effects_.HistorySerial();
    const auto layerSerial = ui_.LayerHistorySerial();
    const auto regionSerial = regions_.HistorySerial();
    const auto triggerSerial = triggers_.HistorySerial();
    const auto spawnSerial = monsterSpawns_.HistorySerial();
    const auto gateSerial = gates_.HistorySerial();

    if (objectSerial != observedObjectHistory_)
    {
        changed.push_back(WorldHistoryDomain::Objects);
        label = document_.UndoLabel();
    }
    if (terrainSerial != observedTerrainHistory_)
    {
        changed.push_back(WorldHistoryDomain::Terrain);
        if (label.empty()) label = "Terrain Stroke";
    }
    if (effectSerial != observedEffectHistory_)
    {
        changed.push_back(WorldHistoryDomain::Effects);
        if (label.empty()) label = "Edit Point Light";
    }
    if (layerSerial != observedLayerHistory_)
    {
        changed.push_back(WorldHistoryDomain::Layers);
        if (label.empty()) label = ui_.LayerUndoLabel();
    }
    if (regionSerial != observedRegionHistory_)
    {
        changed.push_back(WorldHistoryDomain::Regions);
        if (label.empty()) label = regions_.UndoLabel();
    }
    if (triggerSerial != observedTriggerHistory_)
    {
        changed.push_back(WorldHistoryDomain::Triggers);
        if (label.empty()) label = triggers_.UndoLabel();
    }
    if (spawnSerial != observedSpawnHistory_)
    {
        changed.push_back(WorldHistoryDomain::MonsterSpawns);
        if (label.empty()) label = monsterSpawns_.UndoLabel();
    }

    if (gateSerial != observedGateHistory_)
    {
        changed.push_back(WorldHistoryDomain::Gates);
        if (label.empty()) label = "Edit Gate Candidate";
    }
    observedGateHistory_ = gateSerial;
    observedObjectHistory_ = objectSerial;
    observedTerrainHistory_ = terrainSerial;
    observedEffectHistory_ = effectSerial;
    observedLayerHistory_ = layerSerial;
    observedRegionHistory_ = regionSerial;
    observedTriggerHistory_ = triggerSerial;
    observedSpawnHistory_ = spawnSerial;

    if (!changed.empty())
        history_.RecordBatch(std::move(changed), std::move(label));
}

bool WorldEditorSession::UndoGlobal()
{
    const auto* entry = history_.UndoEntry();
    if (!entry) return false;

    for (auto domain : entry->Domains)
    {
        const bool available = domain == WorldHistoryDomain::Objects ? document_.CanUndo()
            : domain == WorldHistoryDomain::Terrain ? terrainDocument_.CanUndo()
            : domain == WorldHistoryDomain::Effects ? effects_.CanUndo()
            : domain == WorldHistoryDomain::Layers ? ui_.Layers().CanUndo()
            : domain == WorldHistoryDomain::Regions ? regions_.CanUndo()
            : domain == WorldHistoryDomain::Triggers ? triggers_.CanUndo()
            : domain == WorldHistoryDomain::MonsterSpawns ? monsterSpawns_.CanUndo()
            : gates_.CanUndo();
        if (!available)
        {
            message_ = "History global desincronizado; Undo cancelado sin aplicar cambios.";
            return false;
        }
    }

    for (auto it = entry->Domains.rbegin(); it != entry->Domains.rend(); ++it)
    {
        switch (*it)
        {
        case WorldHistoryDomain::Objects: document_.Undo(); break;
        case WorldHistoryDomain::Terrain: terrainDocument_.Undo(); break;
        case WorldHistoryDomain::Effects: effects_.Undo(); break;
        case WorldHistoryDomain::Layers: ui_.UndoLayers(); break;
        case WorldHistoryDomain::Regions: regions_.Undo(); break;
        case WorldHistoryDomain::Triggers: triggers_.Undo(); break;
        case WorldHistoryDomain::MonsterSpawns: monsterSpawns_.Undo(); break;
        case WorldHistoryDomain::Gates: gates_.Undo(); break;
        }
    }
    history_.CommitUndo();
    message_ = "Undo global: " + entry->Label;
    return true;
}

bool WorldEditorSession::RedoGlobal()
{
    const auto* entry = history_.RedoEntry();
    if (!entry) return false;

    for (auto domain : entry->Domains)
    {
        const bool available = domain == WorldHistoryDomain::Objects ? document_.CanRedo()
            : domain == WorldHistoryDomain::Terrain ? terrainDocument_.CanRedo()
            : domain == WorldHistoryDomain::Effects ? effects_.CanRedo()
            : domain == WorldHistoryDomain::Layers ? ui_.Layers().CanRedo()
            : domain == WorldHistoryDomain::Regions ? regions_.CanRedo()
            : domain == WorldHistoryDomain::Triggers ? triggers_.CanRedo()
            : domain == WorldHistoryDomain::MonsterSpawns ? monsterSpawns_.CanRedo()
            : gates_.CanRedo();
        if (!available)
        {
            message_ = "History global desincronizado; Redo cancelado sin aplicar cambios.";
            return false;
        }
    }

    for (auto domain : entry->Domains)
    {
        switch (domain)
        {
        case WorldHistoryDomain::Objects: document_.Redo(); break;
        case WorldHistoryDomain::Terrain: terrainDocument_.Redo(); break;
        case WorldHistoryDomain::Effects: effects_.Redo(); break;
        case WorldHistoryDomain::Layers: ui_.RedoLayers(); break;
        case WorldHistoryDomain::Regions: regions_.Redo(); break;
        case WorldHistoryDomain::Triggers: triggers_.Redo(); break;
        case WorldHistoryDomain::MonsterSpawns: monsterSpawns_.Redo(); break;
        case WorldHistoryDomain::Gates: gates_.Redo(); break;
        }
    }
    history_.CommitRedo();
    message_ = "Redo global: " + entry->Label;
    return true;
}

void WorldEditorSession::HandleHistoryHotkeys()
{
    const bool gestureActive = document_.HasPreview() || ui_.Placing()
        || (terrainAvailable_ && terrainDocument_.Active()) || regions_.Editing()
        || regionDragHandle_ >= 0 || spawnDragHandle_ >= 0;
    if (!ImGui::GetIO().WantTextInput && !gestureActive && ImGui::GetIO().KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) UndoGlobal();
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) RedoGlobal();
    }

}

void WorldEditorSession::DrawGlobalHistory()
{
    if (!ImGui::CollapsingHeader("History global", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    const bool gestureActive = document_.HasPreview() || ui_.Placing() || regions_.Editing()
        || (terrainAvailable_ && terrainDocument_.Active());

    ImGui::BeginDisabled(gestureActive || !history_.CanUndo());
    if (ImGui::Button("Deshacer global")) UndoGlobal();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(gestureActive || !history_.CanRedo());
    if (ImGui::Button("Rehacer global")) RedoGlobal();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Z / Ctrl+Y");

    const auto& entries = history_.Entries();
    const std::size_t cursor = history_.Cursor();
    const std::size_t first = entries.size() > 12 ? entries.size() - 12 : 0;
    for (std::size_t index = first; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        std::string domains;
        for (const auto domain : entry.Domains)
        {
            if (!domains.empty()) domains += " + ";
            domains += WorldHistoryCoordinator::DomainName(domain);
        }
        const char* state = index < cursor ? "done" : "redo";
        ImGui::TextDisabled("%s | %s | %s", state, domains.c_str(), entry.Label.c_str());
    }
}

void WorldEditorSession::DrawWorldOutliner()
{
        ImGui::SeparatorText("WORLD / Outliner");
        if (ImGui::TreeNodeEx("WORLD##global", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Selectable("Terrain", tool_ == 1))
            {
                if (terrainAvailable_) tool_ = activeTool_ = 1;
                else message_ = "Este mapa usa altura extendida de 24 bits; Terrain queda read-only.";
            }

            std::size_t activeObjectCount = 0;
            for (const auto& entry : document_.Entries())
                if (entry.Record && !entry.Record->BaselineOnly) ++activeObjectCount;
            const std::string objectsLabel = "Objects (" + std::to_string(activeObjectCount) + ")";
            if (ImGui::TreeNodeEx(objectsLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                std::size_t shown = 0;
                for (const auto& entry : document_.Entries())
                {
                    if (!entry.Record || entry.Record->BaselineOnly) continue;
                    const std::string label = "#" + std::to_string(entry.Id)
                        + " | Type " + std::to_string(entry.Record->Type);
                    if (ImGui::Selectable(label.c_str(), tool_ == 0 && ui_.Selected() == entry.Id))
                    {
                        tool_ = activeTool_ = 0;
                        ui_.SelectOnly(entry.Id);
                    }
                    if (++shown >= 250)
                    {
                        ImGui::TextDisabled("Mostrando 250; usa busqueda/filtros del panel Objects para el resto.");
                        break;
                    }
                }
                ImGui::TreePop();
            }

            if (monsterSpawns_.Loaded())
            {
                const auto drawSpawns = [this](const char* title, bool monsters)
                {
                    std::size_t count = 0;
                    for (const auto& spawn : monsterSpawns_.Entries())
                        if ((spawn.ObjectKind == "Monster") == monsters) ++count;
                    const std::string node = std::string(title) + " (" + std::to_string(count) + ")";
                    if (!ImGui::TreeNodeEx(node.c_str())) return;
                    for (const auto& spawn : monsterSpawns_.Entries())
                    {
                        if ((spawn.ObjectKind == "Monster") != monsters) continue;
                        const std::string label = "#" + std::to_string(spawn.Id) + " | "
                            + spawn.Designation + " | " + spawn.MapVariant + " [" + std::to_string(spawn.MonsterNumber) + "] | Qty "
                            + std::to_string(spawn.Quantity);
                        if (ImGui::Selectable(label.c_str(), selectedSpawn_ == spawn.Id))
                        {
                            selectedSpawn_ = spawn.Id;
                            tool_ = activeTool_ = 5;
                        }
                    }
                    ImGui::TreePop();
                };
                drawSpawns("Monsters", true);
                drawSpawns("NPCs / Guards / Gameplay entities", false);
            }
            else
            {
                ImGui::TreeNodeEx("Monsters  [requiere export actual OpenMU]", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                ImGui::TreeNodeEx("NPCs  [requiere export actual OpenMU]", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            }
            if (gates_.Loaded())
            {
                const std::string gateNode = "Gates (" + std::to_string(gates_.Entries().size()) + ")";
                if (ImGui::TreeNodeEx(gateNode.c_str()))
                {
                    for (const auto& gate : gates_.Entries())
                    {
                        const std::string label = "Gate " + std::to_string(gate.Number)
                            + " | Map " + std::to_string(activeMap_) + " -> "
                            + std::to_string(gate.TargetMap);
                        if (ImGui::Selectable(label.c_str(), selectedGate_ == gate.Id))
                        {
                            selectedGate_ = gate.Id;
                            tool_ = activeTool_ = 6;
                        }
                    }
                    ImGui::TreePop();
                }
            }
            else
                ImGui::TreeNodeEx("Gates  [requiere export actual OpenMU]", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);

            const std::string lightsLabel = "Lights / Effects (" + std::to_string(effects_.Lights().size()) + ")";
            if (ImGui::TreeNodeEx(lightsLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (const auto& light : effects_.Lights())
                {
                    const std::string label = "Point Light #" + std::to_string(light.Id)
                        + " | R " + std::to_string(light.Radius);
                    if (ImGui::Selectable(label.c_str(), tool_ == 3 && effectsUi_.Selected() == light.Id))
                    {
                        tool_ = activeTool_ = 3;
                        effectsUi_.SelectById(effects_, light.Id);
                    }
                }
                ImGui::TreePop();
            }

            const std::string regionsLabel = "Regions (" + std::to_string(regions_.Entries().size()) + ")";
            if (ImGui::TreeNodeEx(regionsLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (const auto& region : regions_.Entries())
                {
                    const std::string shape = region.Shape == WorldRegionShape::Rectangle ? "Rect" : "Circle";
                    const std::string label = "#" + std::to_string(region.Id) + " | " + region.Name
                        + " | " + shape + (region.Tag.empty() ? "" : " | " + region.Tag);
                    if (ImGui::Selectable(label.c_str(), selectedRegion_ == region.Id))
                    {
                        selectedRegion_ = region.Id;
                        tool_ = activeTool_ = 7;
                    }
                }
                ImGui::TreePop();
            }
            ImGui::TreeNodeEx("Collisions / ATT  [Terrain adapter]", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            if (monsterSpawns_.Loaded())
            {
                const std::string spawnsLabel = "Spawns (" + std::to_string(monsterSpawns_.Entries().size()) + ")";
                if (ImGui::TreeNodeEx(spawnsLabel.c_str()))
                {
                    for (const auto& spawn : monsterSpawns_.Entries())
                    {
                        const std::string kind = spawn.IsPoint() ? "Point" : "Area";
                        const std::string label = "#" + std::to_string(spawn.Id) + " | "
                            + spawn.Designation + " | " + kind + " | Qty " + std::to_string(spawn.Quantity);
                        if (ImGui::Selectable(label.c_str(), selectedSpawn_ == spawn.Id))
                        {
                            selectedSpawn_ = spawn.Id;
                            tool_ = activeTool_ = 5;
                        }
                    }
                    ImGui::TreePop();
                }
            }
            else
                ImGui::TreeNodeEx("Spawns  [requiere export actual OpenMU]", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            const std::string triggersLabel = "Triggers (" + std::to_string(triggers_.Entries().size()) + ")";
            if (ImGui::TreeNodeEx(triggersLabel.c_str()))
            {
                for (const auto& trigger : triggers_.Entries())
                {
                    const std::string label = "#" + std::to_string(trigger.Id) + " | " + trigger.Name
                        + " | " + trigger.Event + (trigger.Enabled ? "" : " [disabled]");
                    if (ImGui::Selectable(label.c_str(), selectedTrigger_ == trigger.Id))
                    {
                        selectedTrigger_ = trigger.Id;
                        tool_ = activeTool_ = 8;
                    }
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }

}


void WorldEditorSession::DrawRegionCreation()
{
        ImGui::SeparatorText("Region Editor");
        ImGui::InputTextWithHint("##region-name", "Nombre region...", newRegionName_, sizeof(newRegionName_));
        ImGui::InputTextWithHint("##region-tag", "Tag opcional...", newRegionTag_, sizeof(newRegionTag_));
        ImGui::RadioButton("Rectangle", &newRegionShape_, 0); ImGui::SameLine();
        ImGui::RadioButton("Circle", &newRegionShape_, 1);
        if (newRegionShape_ == 0)
        {
            ImGui::InputFloat2("Min XY", &newRegionValues_[0], "%.1f");
            ImGui::InputFloat2("Max XY", &newRegionValues_[2], "%.1f");
        }
        else
        {
            ImGui::InputFloat2("Center XY", &newRegionValues_[0], "%.1f");
            ImGui::InputFloat("Radius", &newRegionValues_[4], 10.0f, 100.0f, "%.1f");
        }
        ImGui::BeginDisabled(newRegionName_[0] == 0);
        if (ImGui::Button("Crear Region Candidate"))
        {
            try
            {
                selectedRegion_ = newRegionShape_ == 0
                    ? regions_.AddRectangle(newRegionName_, newRegionValues_[0], newRegionValues_[1],
                        newRegionValues_[2], newRegionValues_[3], newRegionTag_)
                    : regions_.AddCircle(newRegionName_, newRegionValues_[0], newRegionValues_[1],
                        newRegionValues_[4], newRegionTag_);
                tool_ = activeTool_ = 7;
                newRegionName_[0] = 0;
                newRegionTag_[0] = 0;
                message_ = "Region creada en Candidate; guardar para persistir.";
            }
            catch (const std::exception& error) { message_ = error.what(); }
        }
        ImGui::EndDisabled();

}


void WorldEditorSession::DrawTriggerCreation()
{
        ImGui::SeparatorText("Trigger Editor");
        ImGui::InputTextWithHint("##trigger-name", "Nombre trigger...", newTriggerName_, sizeof(newTriggerName_));
        ImGui::InputTextWithHint("##trigger-event", "Evento...", newTriggerEvent_, sizeof(newTriggerEvent_));
        const char* regionPreview = "(sin region)";
        if (newTriggerRegionIndex_ >= 0 && newTriggerRegionIndex_ < static_cast<int>(regions_.Entries().size()))
            regionPreview = regions_.Entries()[static_cast<std::size_t>(newTriggerRegionIndex_)].Name.c_str();
        if (ImGui::BeginCombo("Region ref", regionPreview))
        {
            if (ImGui::Selectable("(sin region)", newTriggerRegionIndex_ < 0)) newTriggerRegionIndex_ = -1;
            for (int index = 0; index < static_cast<int>(regions_.Entries().size()); ++index)
                if (ImGui::Selectable(regions_.Entries()[static_cast<std::size_t>(index)].Name.c_str(),
                    newTriggerRegionIndex_ == index)) newTriggerRegionIndex_ = index;
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(newTriggerName_[0] == 0 || newTriggerEvent_[0] == 0);
        if (ImGui::Button("Crear Trigger Candidate"))
        {
            const RegionId region = newTriggerRegionIndex_ >= 0
                ? regions_.Entries()[static_cast<std::size_t>(newTriggerRegionIndex_)].Id : NoRegion;
            try
            {
                selectedTrigger_ = triggers_.Add(newTriggerName_, newTriggerEvent_, region);
                tool_ = activeTool_ = 8;
                newTriggerName_[0] = 0;
                message_ = "Trigger creado en Candidate. Agrega Conditions/Actions desde Inspector.";
            }
            catch (const std::exception& error) { message_ = error.what(); }
        }
        ImGui::EndDisabled();

}


void WorldEditorSession::DrawMapValidation()
{
        ImGui::SeparatorText("Check Map / Validator");
        if (ImGui::Button("Analizar mapa"))
        {
            validationReport_ = WorldMapValidator::Validate(document_, effects_, &monsterSpawns_, &gates_, &regions_, &triggers_, &ui_.Layers(), terrainAvailable_ ? &terrainDocument_ : nullptr);
            message_ = "Check Map: " + std::to_string(validationReport_->Errors) + " errores, "
                + std::to_string(validationReport_->Warnings) + " advertencias.";
        }
        if (validationReport_)
        {
            ImGui::SameLine();
            ImGui::Text("%zu errores | %zu advertencias",
                validationReport_->Errors, validationReport_->Warnings);
            if (ImGui::BeginChild("check-map-results", {0, 120}, ImGuiChildFlags_Borders))
            {
                for (std::size_t index = 0; index < validationReport_->Issues.size(); ++index)
                {
                    const auto& issue = validationReport_->Issues[index];
                    const char* severity = issue.Severity == WorldValidationSeverity::Error ? "ERROR"
                        : (issue.Severity == WorldValidationSeverity::Warning ? "WARN" : "OK");
                    std::string label = std::string(severity) + " | " + issue.Message;
                    if (issue.Id != 0) label += " (#" + std::to_string(issue.Id) + ")";
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Selectable(label.c_str()))
                    {
                        if (issue.Entity == WorldValidationEntity::Object && document_.Find(static_cast<ObjectId>(issue.Id)))
                        {
                            tool_ = activeTool_ = 0;
                            ui_.SelectOnly(static_cast<ObjectId>(issue.Id));
                        }
                        else if (issue.Entity == WorldValidationEntity::Light && effects_.Find(issue.Id))
                        {
                            tool_ = activeTool_ = 3;
                            effectsUi_.SelectById(effects_, issue.Id);
                        }
                        else if (issue.Entity == WorldValidationEntity::MonsterSpawn && monsterSpawns_.Find(issue.Id))
                        {
                            selectedSpawn_ = issue.Id;
                            tool_ = activeTool_ = 5;
                        }
                        else if (issue.Entity == WorldValidationEntity::Gate && gates_.Find(issue.Id))
                        {
                            selectedGate_ = issue.Id;
                            tool_ = activeTool_ = 6;
                        }
                        else if (issue.Entity == WorldValidationEntity::Region && regions_.Find(issue.Id))
                        {
                            selectedRegion_ = issue.Id;
                            tool_ = activeTool_ = 7;
                        }
                        else if (issue.Entity == WorldValidationEntity::Trigger && triggers_.Find(issue.Id))
                        {
                            selectedTrigger_ = issue.Id;
                            tool_ = activeTool_ = 8;
                        }
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }

}

bool WorldEditorSession::DrawTools()
{
    bool save = DrawRibbon();
    HandleHistoryHotkeys();
    DrawCommandPalette();
    DrawGlobalSearch();
    DrawChangeReview();
    DrawGameplayOverlays();
    HandleRegionViewportEdit();
    HandleSpawnViewportEdit();
    DrawGuide();
    auto& layout = ui_.WorkspaceLayout();
    if (layout.Visible(WorkspacePanel::Details))
    {
        if (Workspace::Begin("Herramientas de mapa###WorldDetails", layout, WorkspacePanel::Details))
        {
            const char* sections[]{"Escena", "Propiedades", "Revisar", "Ajustes"};
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##detail-section", &toolDetailsTab_, sections, 4);
            ImGui::Separator();
            if (toolDetailsTab_ == 0) DrawWorldOutliner();
            else if (toolDetailsTab_ == 1)
            {
                DrawUniversalInspector();
                if (tool_ == 7) DrawRegionCreation();
                if (tool_ == 8) DrawTriggerCreation();
            }
            else if (toolDetailsTab_ == 2) { DrawGlobalHistory(); DrawMapValidation(); }
            else { DrawResolutionControls(); DrawPerformanceAnalyzer(); }
        }
        ImGui::End();
    }
    DrawStatusBar();
    return save;
}

void WorldEditorSession::DrawRibbonPanels()
{
    auto& layout = ui_.WorkspaceLayout();
    if (ImGui::Button("Paneles")) ImGui::OpenPopup("workspace-panels");
    if (ImGui::BeginPopup("workspace-panels"))
    {
        constexpr const char* names[]{"Escena y capas", "Propiedades de objetos", "Modelos del mapa", "Herramientas de mapa"};
        for (std::size_t i = 0; i < layout.Panels.size(); ++i)
            if (ImGui::MenuItem(names[i], nullptr, &layout.Panels[i])) layout.FocusMode = false;
        ImGui::MenuItem("Opciones de herramienta activa", nullptr, &toolOptionsOpen_);
        ImGui::Separator();
        if (ImGui::MenuItem("Restablecer espacio de trabajo"))
        { layout.Restore(); toolOptionsOpen_ = true; ++toolLayoutRevision_; }
        ImGui::TextDisabled("Arrastra los titulos para mover. Triangulo: minimizar. X: ocultar.");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Solo mapa [Tab]", &layout.FocusMode);
    ImGui::SameLine();
    if (ImGui::Button(layout.RibbonCollapsed ? "Expandir cinta" : "Contraer cinta")) layout.RibbonCollapsed = !layout.RibbonCollapsed;
}

void WorldEditorSession::DrawRibbonCamera()
{
    constexpr const char* views[]{"3D isometrica", "Superior", "Frontal", "Lateral"};
    for (int view = 0; view < 4; ++view)
    {
        if (view) ImGui::SameLine();
        if (ImGui::Button(views[view]) && !scene_.SetEditorView(view)) message_ = "No se pudo cambiar la vista.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Centrar [F11]")) ResetCameraHotkey();
    ImGui::SameLine();
    ImGui::Checkbox("Girar con ruedita / derecho [F10]", &freeLookEnabled_);
}

void WorldEditorSession::DrawRibbonObjects()
{
    if (ImGui::Button("Catalogo completo")) { tool_ = 2; toolOptionsOpen_ = true; }
    ImGui::SameLine(); if (ImGui::Button("Colocados / seleccionar")) { tool_ = 0; ui_.WorkspaceLayout().Open(WorkspacePanel::Scene) = true; }
    ImGui::SameLine(); if (ImGui::Button("Mejorar seleccion con IA")) { tool_ = 4; toolOptionsOpen_ = true; }
    ImGui::SameLine(); if (ImGui::Button("Efectos y luces")) { tool_ = 3; toolOptionsOpen_ = true; }
}
void WorldEditorSession::DrawRibbonFileActions(bool gesture)
{
    ImGui::BeginDisabled(gesture);
    if (ImGui::Button("Cargar mapa")) backRequested_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Resolucion")) ImGui::OpenPopup("Resolucion de ventana");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("Resolucion de ventana"))
    {
        ImGui::SeparatorText("Tamano de la ventana del editor");
        DrawResolutionControls();
        ImGui::Separator();
        if (ImGui::Button("Cerrar")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

bool WorldEditorSession::DrawRibbon()
{
    bool save = false;
    auto& layout = ui_.WorkspaceLayout();
    const auto& io = ImGui::GetIO();
    if (!io.WantTextInput && !io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
        layout.FocusMode = !layout.FocusMode;
    ImGui::SetNextWindowPos({0, WorldWorkspaceLayout::RibbonTop}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({io.DisplaySize.x, layout.RibbonCollapsed ? 38.0f : WorldWorkspaceLayout::RibbonHeight}, ImGuiCond_Always);
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.055f, 0.075f, 0.095f, 1.0f});
    if (ImGui::Begin("Cinta Silver World Editor", nullptr, flags))
    {
        constexpr const char* tabs[]{"Inicio", "Terreno", "Objetos", "Efectos", "Logica", "Camara", "Revisar", "NPC / Mobs"};
        constexpr int tools[]{0, 1, 2, 3, 6, -1, -1, 5};
        const bool gesture = document_.HasPreview() || ui_.Placing() || (terrainAvailable_ && terrainDocument_.Active());
        DrawRibbonFileActions(gesture);
        ImGui::SameLine();
        ImGui::BeginDisabled(gesture);
        for (int tab = 0; tab < 8; ++tab)
        {
            if (tab) ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, tab == ribbonTab_ ? ImVec4{0.12f,0.40f,0.53f,1} : ImVec4{0.12f,0.16f,0.20f,1});
            ImGui::BeginDisabled(tab == 1 && !terrainAvailable_);
            if (ImGui::Button(tabs[tab]))
            {
                ribbonTab_ = tab; layout.RibbonCollapsed = false;
                if (tools[tab] >= 0) { tool_ = tools[tab]; toolOptionsOpen_ = true; layout.FocusMode = false; }
                if (tab == 4 || tab == 6)
                { layout.Open(WorkspacePanel::Details) = true; toolDetailsTab_ = tab == 4 ? 0 : 2; layout.FocusMode = false; }
            }
            ImGui::EndDisabled(); ImGui::PopStyleColor();
        }
        ImGui::EndDisabled();
        ImGui::SameLine(); DrawRibbonPanels();
        if (!layout.RibbonCollapsed)
        {
            ImGui::Separator();
            if (ribbonTab_ == 5) DrawRibbonCamera();
            else if (ribbonTab_ == 2) DrawRibbonObjects();
            else if (ribbonTab_ == 7)
            {
                if(ImGui::Button("Catalogo del servidor")){tool_=5;toolOptionsOpen_=true;}
                ImGui::SameLine();if(ImGui::Button("Spawns colocados")){tool_=5;toolDetailsTab_=0;layout.Open(WorkspacePanel::Details)=true;}
            }
            else if (ribbonTab_ == 0)
            {
                ImGui::BeginDisabled(gesture);
                if (ImGui::Button("Guardar copia [Ctrl+S]")) save = true;
                ImGui::SameLine(); if (ImGui::Button("Mover")) ui_.SetOperation(0);
                ImGui::SameLine(); if (ImGui::Button("Rotar")) ui_.SetOperation(1);
                ImGui::SameLine(); if (ImGui::Button("Escalar")) ui_.SetOperation(2);
                ImGui::SameLine(); if (ImGui::Button("CAD / Distribuir")) ui_.OpenCad();
                ImGui::EndDisabled();
                ImGui::SameLine(); if (ImGui::Button("Modelos")) layout.Open(WorkspacePanel::Assets) = !layout.Open(WorkspacePanel::Assets);
            }
            else if (ribbonTab_ == 4)
            {
                constexpr const char* names[]{"Monstruos / NPC", "Gates", "Regiones", "Triggers"};
                for (int index = 0; index < 4; ++index)
                {
                    if (index) ImGui::SameLine();
                    ImGui::BeginDisabled(gesture);
                    if (ImGui::Button(names[index]))
                    { tool_ = index + 5; toolDetailsTab_ = 0; layout.Open(WorkspacePanel::Details) = true; }
                    ImGui::EndDisabled();
                }
                ImGui::SameLine(); if (ImGui::Button("Propiedades")) toolDetailsTab_ = 1;
            }
            else if (ribbonTab_ == 6)
            {
                if (ImGui::Button("Validar / Historial")) { toolDetailsTab_ = 2; layout.Open(WorkspacePanel::Details) = true; }
                ImGui::SameLine(); if (ImGui::Button("Rendimiento / Resolucion")) { toolDetailsTab_ = 3; layout.Open(WorkspacePanel::Details) = true; }
                ImGui::SameLine(); if (ImGui::Button("Revisar cambios")) showChangeReview_ = true;
            }
            else
            {
                ImGui::Checkbox("Mostrar opciones de herramienta", &toolOptionsOpen_);
                ImGui::SameLine(); ImGui::Checkbox("Cuadricula", &showViewportGrid_);
                ImGui::SameLine(); if (ImGui::Button("Asistente IA")) { tool_ = 4; toolOptionsOpen_ = true; }
            }
            ImGui::SameLine(); if (ImGui::Button("Guia rapida")) openGuidePopup_ = true;
            ImGui::TextColored({0.72f,0.84f,0.9f,1}, "Camara: ruedita apretada o derecho + arrastrar | WASD: mover | rueda: zoom | PgUp/PgDn: altura | F: enfocar objeto");
        }
        if (!gesture && !io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save = true;
    }
    ImGui::End(); ImGui::PopStyleColor();
    return save;
}

void WorldEditorSession::DrawStatusBar()
{
    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, io.DisplaySize.y - WorldWorkspaceLayout::StatusHeight}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({io.DisplaySize.x, WorldWorkspaceLayout::StatusHeight}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.035f,0.055f,0.075f,1});
    if (ImGui::Begin("Estado del mapa", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        const float fps = io.Framerate;
        ImGui::TextColored({0.4f,1.0f,0.7f,1}, "%.0f FPS | %.1f ms", fps, fps > 0 ? 1000.0f / fps : 0.0f);
        ImGui::SameLine();
        ImGui::Text("| World%d | %s | %zu seleccionados", activeWorldNumber_, Dirty() ? "CAMBIOS SIN GUARDAR" : "Guardado", ui_.SelectedItems().size());
        ImGui::SameLine(); ImGui::TextUnformatted(message_.c_str());
    }
    ImGui::End(); ImGui::PopStyleColor();
}

void WorldEditorSession::UpdateEffectPlacement()
{
    if (!placingEffect_ || tool_ != 3) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { placingEffect_ = false; return; }
    const auto& io = ImGui::GetIO();
    if (io.AppFocusLost || io.WantCaptureMouse || io.WantTextInput || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) return;
    if (!scene_.Placement(io.MousePos.x,io.MousePos.y,runtimeEffectPosition_)) return;
    runtimeEffectPositionValid_ = true;
    const auto& preset = WorldRuntimeEffectCatalog::Presets().at(static_cast<std::size_t>(runtimeEffectPreset_));
    const int kind = preset.Kind == WorldRuntimeEffectKind::Particle ? 0 : 1;
    if (ImGui::GetTime() - effectPreviewTime_ >= 0.4)
    { scene_.PreviewRuntimeEffect(kind,preset.Type,preset.SubType,runtimeEffectPosition_,runtimeEffectScale_); effectPreviewTime_ = ImGui::GetTime(); }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        WorldRuntimeEffect effect; effect.Preset=preset.Name; effect.Kind=kind; effect.Type=preset.Type;
        effect.SubType=preset.SubType; effect.Position=runtimeEffectPosition_; effect.Scale=runtimeEffectScale_;
        effects_.AddRuntimeEffect(effect); message_="Efecto colocado. Ctrl+Z deshace; Ctrl+S guarda.";
    }
}
void WorldEditorSession::DrawRuntimeEffects(bool* open)
{
            ImGui::SetNextWindowSize({430,300},ImGuiCond_FirstUseEver);
            if(ImGui::Begin("Runtime Effects | Preview", open))
            {
                const auto& presets=WorldRuntimeEffectCatalog::Presets();
                if(runtimeEffectPreset_<0||runtimeEffectPreset_>=static_cast<int>(presets.size()))runtimeEffectPreset_=0;
                if(ImGui::BeginCombo("Preset verificado",presets[static_cast<std::size_t>(runtimeEffectPreset_)].Name.c_str()))
                {
                    for(int i=0;i<static_cast<int>(presets.size());++i)if(ImGui::Selectable(presets[static_cast<std::size_t>(i)].Name.c_str(),i==runtimeEffectPreset_))runtimeEffectPreset_=i;
                    ImGui::EndCombo();
                }
                ImGui::Checkbox("Colocar efecto con el mouse", &placingEffect_);
                ImGui::TextWrapped("Clic sobre el suelo coloca; Esc termina. La vista previa sigue al mouse.");
                ImGui::SliderFloat("Scale preview",&runtimeEffectScale_,0.1f,5.0f,"%.2f");
                const auto& preset=presets[static_cast<std::size_t>(runtimeEffectPreset_)];
                ImGui::Text("Runtime: %s | Type %d | SubType %d",preset.Kind==WorldRuntimeEffectKind::Particle?"Particle":"Effect",preset.Type,preset.SubType);
                ImGui::TextWrapped("Evidencia: %s",preset.Evidence.c_str());
                if(ImGui::Button("Tomar posicion de camara"))
                    runtimeEffectPositionValid_=scene_.CameraMapPosition(runtimeEffectPosition_);
                ImGui::SameLine();
                ImGui::BeginDisabled(!runtimeEffectPositionValid_);
                if(ImGui::Button("Preview una vez"))
                {
                    const int kind=preset.Kind==WorldRuntimeEffectKind::Particle?0:1;
                    if(!scene_.PreviewRuntimeEffect(kind,preset.Type,preset.SubType,runtimeEffectPosition_,runtimeEffectScale_))
                        message_="El runtime rechazo el preview del efecto.";
                    else message_="Preview runtime emitido una vez. No fue persistido en Candidate.";
                }
                ImGui::EndDisabled();
                ImGui::SameLine();ImGui::BeginDisabled(!runtimeEffectPositionValid_);
                if(ImGui::Button("Agregar a Candidate"))
                {
                    try{WorldRuntimeEffect effect;effect.Preset=preset.Name;effect.Kind=preset.Kind==WorldRuntimeEffectKind::Particle?0:1;effect.Type=preset.Type;effect.SubType=preset.SubType;effect.Position=runtimeEffectPosition_;effect.Scale=runtimeEffectScale_;effects_.AddRuntimeEffect(effect);message_="Runtime effect agregado al sidecar Candidate. Data original intacta.";}
                    catch(const std::exception& error){message_=error.what();}
                }
                ImGui::EndDisabled();
                if(runtimeEffectPositionValid_)ImGui::Text("Pos: %.0f %.0f %.0f",runtimeEffectPosition_[0],runtimeEffectPosition_[1],runtimeEffectPosition_[2]);
                ImGui::SeparatorText("Runtime effects Candidate");
                for(const auto& effect:effects_.RuntimeEffects())
                {
                    ImGui::PushID(static_cast<int>(effect.Id));ImGui::Text("#%llu %s | %.0f %.0f %.0f | x%.2f",static_cast<unsigned long long>(effect.Id),effect.Preset.c_str(),effect.Position[0],effect.Position[1],effect.Position[2],effect.Scale);
                    ImGui::SameLine();if(ImGui::SmallButton("Preview"))scene_.PreviewRuntimeEffect(effect.Kind,effect.Type,effect.SubType,effect.Position,effect.Scale);
                    ImGui::SameLine();if(ImGui::SmallButton("Eliminar")){effects_.RemoveRuntimeEffect(effect.Id);ImGui::PopID();break;}ImGui::PopID();
                }
                ImGui::TextDisabled("Candidate persistente en sidecar versionado; runtime preview usa CreateParticle/CreateEffect reales. Data no se modifica.");
            }
            ImGui::End();
}

void WorldEditorSession::ApplyEffects()
{
    if (!authorized_ || !visible_ || !editing_
        || SceneFlag != MAIN_SCENE || gMapManager.WorldActive != activeMap_)
        return;

    for (const auto& effect : effects_.Lights())
    {
        vec3_t light;
        Vector(effect.Color[0] * effect.Intensity,
            effect.Color[1] * effect.Intensity,
            effect.Color[2] * effect.Intensity,
            light);
        AddTerrainLight(effect.Position[0], effect.Position[1],
            light, effect.Radius, PrimaryTerrainLight);
    }
}

void WorldEditorSession::RenderPreview()
{
    if (!g_MuEditorCore.IsEnabled() || !authorized_ || !visible_ || !editing_
        || SceneFlag != MAIN_SCENE || gMapManager.WorldActive != activeMap_) return;
    const auto& io = ImGui::GetIO();
    if (!scene_.DrawLibraryObjects()) message_ = "No se pudo dibujar un objeto de biblioteca.";
    if (io.AppFocusLost || io.WantCaptureMouse || io.WantTextInput) return;
    if (!scene_.DrawPlacementPreview())
    {
        ui_.CancelPlacement();
        scene_.SetPlacementPreview(std::nullopt);
        message_ = "No se pudo dibujar la vista fantasma. Colocacion cancelada; no se agrego un objeto.";
    }
}


void WorldEditorSession::PublishDiagnostics()
{
    static auto nextWrite = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now < nextWrite) return;
    nextWrite = now + std::chrono::milliseconds(500);
    try
    {
        wchar_t tempPath[MAX_PATH]{};
        const DWORD count = GetTempPathW(MAX_PATH, tempPath);
        std::filesystem::path base = (count > 0 && count < MAX_PATH) ? tempPath : L".";
        const auto directory = base / L"SilverMU" / L"diagnostics";
        std::filesystem::create_directories(directory);
        const auto output = directory / L"editor.json";
        const auto pending = directory / L"editor.json.tmp";
        std::array<float,3> camera{};
        const bool hasCamera = scene_.Attached() && scene_.CameraMapPosition(camera);
        std::ofstream stream(pending, std::ios::binary | std::ios::trunc);
        if (!stream) return;
        stream << "{\"schema\":1,\"component\":\"SilverWorldEditor\",\"pid\":" << GetCurrentProcessId()
               << ",\"authorized\":" << (authorized_ ? "true" : "false")
               << ",\"visible\":" << (visible_ ? "true" : "false")
               << ",\"editing\":" << (editing_ ? "true" : "false")
               << ",\"dirty\":" << (Dirty() ? "true" : "false")
               << ",\"map\":" << activeMap_ << ",\"world\":" << activeWorldNumber_
               << ",\"selectedObject\":" << ui_.Selected()
               << ",\"selectedSpawn\":" << selectedSpawn_
               << ",\"selectedRegion\":" << selectedRegion_
               << ",\"selectedTrigger\":" << selectedTrigger_
               << ",\"selectedGate\":" << selectedGate_
               << ",\"camera\":{\"available\":" << (hasCamera ? "true" : "false")
               << ",\"x\":" << camera[0] << ",\"y\":" << camera[1] << ",\"z\":" << camera[2] << "}}\n";
        stream.close();
        std::error_code ec;
        std::filesystem::rename(pending, output, ec);
        if (ec) { std::filesystem::remove(output, ec); ec.clear(); std::filesystem::rename(pending, output, ec); }
    }
    catch (...) { }
}
void WorldEditorSession::Update()
{
    PublishDiagnostics();
    if (!authorized_ || !visible_ || SceneFlag != MAIN_SCENE)
        return;

    if (!IsStandaloneMode()
        && (!szServerIpAddress || !IsAllowedEditorEndpoint(szServerIpAddress, g_ServerPort)))
        return;

    if (standaloneWorldLoadPending_)
    {
        standaloneWorldLoadPending_ = false;
        if (pendingMap_ < 0)
        {
            mu::WaitForSDLGpuIdle();
            g_ErrorReport.Write(L"> World Editor pre-frame: loading standalone Lorencia base world.\r\n");
            gMapManager.WorldActive = 0;
            gMapManager.LoadWorld(0);
            return;
        }

        // Background diagnostics already queued Lorencia as an editable map.
        // Fold the base-world bootstrap into that one safe LoadAndOpen call
        // instead of loading Lorencia twice on consecutive frames.
        g_ErrorReport.Write(L"> World Editor pre-frame: folding base load into pending editable map.\r\n");
    }

    // Map loading mutates and releases renderer resources (models/textures).
    // It runs only from the pre-frame hook, before SDL_GPU BeginFrame acquires
    // a command buffer, never from Draw()/ImGui or an active render frame.
    if (!editing_ && pendingMap_ >= 0)
    {
        const int requested = pendingMap_;
        pendingMap_ = -1;
        try
        {
            if (requested < 0 || requested >= static_cast<int>(maps_.size()))
                throw std::out_of_range("Seleccion de mapa invalida.");
            LoadAndOpen(maps_[static_cast<std::size_t>(requested)]);
        }
        catch (const std::exception& error)
        {
            message_ = error.what();
        }
        return;
    }

    if (!editing_ || !BackgroundEditorRequested())
        return;

    if (backgroundSwitchStage_ > 0 && backgroundSwitchStage_ < 4)
    {
        ++backgroundSwitchFrames_;
        if (backgroundSwitchFrames_ < 8)
            return;

        if (backgroundSwitchStage_ == 1 && activeMap_ == 0)
        {
            const auto alternate = std::find_if(maps_.begin(), maps_.end(),
                [](const WorldMapEntry& entry) { return entry.MapId != 0; });
            if (alternate == maps_.end())
            {
                backgroundSwitchStage_ = 5;
                message_ = "Diagnostico multi-mapa FAIL: no hay segundo mapa disponible.";
                g_ErrorReport.Write(L"> Diagnostico multi-mapa FAIL: no hay segundo mapa disponible.\r\n");
                Destroy = true;
                return;
            }

            const int index = static_cast<int>(std::distance(maps_.begin(), alternate));
            backgroundAlternateMap_ = alternate->MapId;
            backgroundSwitchStage_ = 2;
            backgroundSwitchFrames_ = 0;
            g_ErrorReport.Write(L"> Diagnostico multi-mapa STEP: Lorencia -> Map %d.\r\n", backgroundAlternateMap_);
            QueueMapSwitch(index);
            return;
        }

        if (backgroundSwitchStage_ == 2 && activeMap_ == backgroundAlternateMap_)
        {
            const auto lorencia = std::find_if(maps_.begin(), maps_.end(),
                [](const WorldMapEntry& entry) { return entry.MapId == 0; });
            if (lorencia == maps_.end())
            {
                backgroundSwitchStage_ = 5;
                message_ = "Diagnostico multi-mapa FAIL: Lorencia desaparecio del catalogo.";
                g_ErrorReport.Write(L"> Diagnostico multi-mapa FAIL: Lorencia desaparecio del catalogo.\r\n");
                Destroy = true;
                return;
            }

            backgroundSwitchStage_ = 3;
            backgroundSwitchFrames_ = 0;
            g_ErrorReport.Write(L"> Diagnostico multi-mapa STEP: Map %d -> Lorencia.\r\n", backgroundAlternateMap_);
            QueueMapSwitch(static_cast<int>(std::distance(maps_.begin(), lorencia)));
            return;
        }

        if (backgroundSwitchStage_ == 3 && activeMap_ == 0)
        {
            backgroundSwitchStage_ = 4;
            backgroundSwitchFrames_ = 0;
            backgroundDiagnosticPassed_ = true;
            message_ = "Diagnostico multi-mapa PASS: Lorencia -> otro mapa -> Lorencia.";
            g_ErrorReport.Write(L"> Diagnostico multi-mapa PASS: Lorencia -> otro mapa -> Lorencia.\r\n");
            return;
        }
    }

    if (backgroundSwitchStage_ == 4 && backgroundDiagnosticPassed_)
    {
        ++backgroundSwitchFrames_;
        if (backgroundSwitchFrames_ >= 1200)
        {
            backgroundSwitchStage_ = 5;
            g_ErrorReport.Write(L"> Diagnostico multi-mapa STABLE PASS: 1200 frames posteriores sin crash.\r\n");
            Destroy = true;
        }
    }
}

void WorldEditorSession::Draw(bool editorVisible)
{
    if (!authorized_) return;

    if (!editorVisible && visible_)
    {
        document_.Cancel();
        if (terrainAvailable_) terrainUi_.Cancel(terrainDocument_);
        try
        {
            if (scene_.Attached())
            {
                scene_.Synchronize(document_);
                if (terrainAvailable_) terrainScene_.Synchronize(terrainDocument_);
            }
        }
        catch (const std::exception& error)
        {
            message_ = error.what();
            scene_.Invalidate();
            terrainScene_.Invalidate();
            editing_ = false;
            activeMap_ = -1;
            activeWorldNumber_ = -1;
        }
        ui_.Reset();
        LeaveEditorCamera();
    }

    visible_ = editorVisible;
    if (!visible_) return;

    if (!IsStandaloneMode()
        && (!szServerIpAddress || !IsAllowedEditorEndpoint(szServerIpAddress, g_ServerPort)))
    {
        if (editing_) BeforeMapChange();
        return;
    }

    if (editing_ && (SceneFlag != MAIN_SCENE || gMapManager.WorldActive != activeMap_))
        BeforeMapChange();

    // Reopening F12 must restore the editor camera even when the scene stayed loaded.
    if (editing_ && !editorCameraActive_)
        EnterEditorCamera();

    if (!editing_)
    {
        DrawEntry();
        return;
    }

    try
    {
        UpdateEffectPlacement();
        scene_.Synchronize(document_);
        if (terrainAvailable_) terrainScene_.Synchronize(terrainDocument_);

        bool save = DrawTools();
        if (backRequested_)
        {
            CloseToSelector();
            return;
        }

        if (tool_ != activeTool_)
        {
            ui_.CancelPlacement();
            scene_.SetPlacementPreview(std::nullopt);
            if (terrainAvailable_) terrainUi_.Cancel(terrainDocument_);
            if (regions_.Editing()) regions_.CancelEdit();
            regionDragHandle_ = -1;
            spawnDragHandle_ = -1;
            activeTool_ = tool_;
        }

        const bool wasOptionsVisible = toolOptionsOpen_ && !ui_.WorkspaceLayout().FocusMode;
        bool optionsVisible = wasOptionsVisible;
        const bool resetTool = toolLayoutApplied_ != toolLayoutRevision_;
        toolLayoutApplied_ = toolLayoutRevision_;
        if (tool_ == 0)
            save = ui_.Draw(document_, scene_) || save;
        else if (tool_ == 1 && terrainAvailable_)
            save = terrainUi_.Draw(terrainDocument_, scene_, &optionsVisible, resetTool) || save;
        else if (tool_ == 2 && optionsVisible)
        {
            if (const auto* model = libraryUi_.Draw(scene_, &optionsVisible, resetTool))
            {
                ui_.ChoosePrototype(*model, document_);
                tool_ = activeTool_ = 0;
            }
        }
        else if (tool_ == 5 && optionsVisible)
            npcLibraryUi_.Draw(monsterSpawns_, &optionsVisible);
        else if (tool_ == 3 && optionsVisible)
        {
            save = effectsUi_.Draw(effects_, scene_, &optionsVisible, resetTool) || save;
            DrawRuntimeEffects(&optionsVisible);
        }
        else if (tool_ == 4 && optionsVisible)
        {
            if(ImGui::Begin("AI Command Batch", &optionsVisible))
            {
                ImGui::TextDisabled("Importa solo propuestas JSON Candidate-only. Nunca auto-aplica.");
                if(ImGui::Button("Cargar ultimo command-batch.json"))
                {
                    try
                    {
                        const auto path=std::filesystem::path("EditorCandidates")/"AI"/"command-batch.json";
                        const auto bytes=ReadCandidateJson(path);auto batch=WorldAiCommandBatch::Decode(bytes);
                        if(batch.MapId!=activeMap_||batch.WorldNumber!=activeWorldNumber_)throw std::runtime_error("AI CommandBatch pertenece a otro mapa.");
                        std::string error;if(!batch.Validate(document_,error))throw std::runtime_error(error);
                        aiCommandBatch_=std::move(batch);aiCommandBatchStatus_="AI CommandBatch validado; pendiente de confirmacion manual.";
                    }
                    catch(const std::exception& error){aiCommandBatch_.reset();aiCommandBatchStatus_=error.what();}
                }
                if(aiCommandBatch_)
                {
                    ImGui::Text("Request %s | %zu comandos",aiCommandBatch_->RequestId.c_str(),aiCommandBatch_->Commands.size());
                    if(ImGui::BeginChild("ai-command-list",{0,180},ImGuiChildFlags_Borders))
                    {
                        for(const auto& command:aiCommandBatch_->Commands)
                        {
                            ImGui::Text("Object #%llu Type %d",static_cast<unsigned long long>(command.Id),command.Expected.Type);
                            if(command.Expected.Position!=command.Desired.Position)ImGui::BulletText("Pos %.1f %.1f %.1f => %.1f %.1f %.1f",command.Expected.Position[0],command.Expected.Position[1],command.Expected.Position[2],command.Desired.Position[0],command.Desired.Position[1],command.Desired.Position[2]);
                            if(command.Expected.Angle!=command.Desired.Angle)ImGui::BulletText("Rot %.1f %.1f %.1f => %.1f %.1f %.1f",command.Expected.Angle[0],command.Expected.Angle[1],command.Expected.Angle[2],command.Desired.Angle[0],command.Desired.Angle[1],command.Desired.Angle[2]);
                            if(command.Expected.Scale!=command.Desired.Scale)ImGui::BulletText("Scale %.3f => %.3f",command.Expected.Scale,command.Desired.Scale);
                        }
                    }
                    ImGui::EndChild();
                    if(ImGui::Button("Aceptar AI CommandBatch"))
                    {
                        std::string error;if(aiCommandBatch_->Apply(document_,error)){aiCommandBatchStatus_="AI CommandBatch aplicado como una transaccion Undo/Redo Candidate.";aiCommandBatch_.reset();}else aiCommandBatchStatus_=error;
                    }
                    ImGui::SameLine();if(ImGui::Button("Rechazar")){aiCommandBatch_.reset();aiCommandBatchStatus_="AI CommandBatch rechazado; mapa sin cambios adicionales.";}
                }
                ImGui::SeparatorText("Gameplay AI");
                if(ImGui::Button("Cargar ai-gameplay-batch.json"))
                {
                    try{auto batch=WorldAiGameplayBatch::Decode(ReadCandidateJson(std::filesystem::path("EditorCandidates")/"AI"/"ai-gameplay-batch.json"));if(batch.MapId!=activeMap_)throw std::runtime_error("AI gameplay batch pertenece a otro mapa.");std::string error;if(!batch.Validate(regions_,triggers_,error))throw std::runtime_error(error);aiGameplayBatch_=std::move(batch);aiCommandBatchStatus_="AI gameplay batch validado; pendiente de confirmacion.";}
                    catch(const std::exception& error){aiGameplayBatch_.reset();aiCommandBatchStatus_=error.what();}
                }
                if(aiGameplayBatch_)
                {
                    ImGui::Text("%zu Regions | %zu Triggers",aiGameplayBatch_->Regions.size(),aiGameplayBatch_->Triggers.size());
                    for(const auto& command:aiGameplayBatch_->Regions)ImGui::BulletText("Region #%llu: %s => %s",static_cast<unsigned long long>(command.Id),command.Expected.Name.c_str(),command.Desired.Name.c_str());
                    for(const auto& command:aiGameplayBatch_->Triggers)ImGui::BulletText("Trigger #%llu: %s => %s",static_cast<unsigned long long>(command.Id),command.Expected.Name.c_str(),command.Desired.Name.c_str());
                    if(ImGui::Button("Aceptar Gameplay Batch")){std::string error;if(aiGameplayBatch_->Apply(regions_,triggers_,error)){aiCommandBatchStatus_="Gameplay AI aplicado a Candidate; History global captura Regions/Triggers.";aiGameplayBatch_.reset();}else aiCommandBatchStatus_=error;}
                    ImGui::SameLine();if(ImGui::Button("Rechazar Gameplay")){aiGameplayBatch_.reset();aiCommandBatchStatus_="Gameplay AI rechazado.";}
                }
                ImGui::SeparatorText("Monster/NPC Spawn AI");
                ImGui::BeginDisabled(!monsterSpawns_.Loaded());
                if(ImGui::Button("Cargar ai-spawn-batch.json"))
                {
                    try{auto batch=WorldAiSpawnBatch::Decode(ReadCandidateJson(std::filesystem::path("EditorCandidates")/"AI"/"ai-spawn-batch.json"));std::string error;if(!batch.Validate(monsterSpawns_,error))throw std::runtime_error(error);aiSpawnBatch_=std::move(batch);aiCommandBatchStatus_="AI SpawnBatch validado; identidad OpenMU preservada.";}
                    catch(const std::exception& error){aiSpawnBatch_.reset();aiCommandBatchStatus_=error.what();}
                }
                ImGui::EndDisabled();
                if(aiSpawnBatch_)
                {
                    ImGui::Text("%zu Spawn commands",aiSpawnBatch_->Commands.size());
                    if(ImGui::BeginChild("ai-spawn-list",{0,160},ImGuiChildFlags_Borders))
                    {
                        for(const auto& command:aiSpawnBatch_->Commands){ImGui::Text("#%llu %s | Monster %d",static_cast<unsigned long long>(command.Id),command.Expected.Designation.c_str(),command.Expected.MonsterNumber);ImGui::BulletText("Area %u,%u-%u,%u => %u,%u-%u,%u | Qty %d => %d",command.Expected.X1,command.Expected.Y1,command.Expected.X2,command.Expected.Y2,command.Desired.X1,command.Desired.Y1,command.Desired.X2,command.Desired.Y2,command.Expected.Quantity,command.Desired.Quantity);}
                    }
                    ImGui::EndChild();
                    if(ImGui::Button("Aceptar Spawn Batch")){std::string error;if(aiSpawnBatch_->Apply(monsterSpawns_,error)){aiCommandBatchStatus_="AI SpawnBatch aplicado a Candidate OpenMU; Apply servidor sigue bloqueado.";aiSpawnBatch_.reset();}else aiCommandBatchStatus_=error;}
                    ImGui::SameLine();if(ImGui::Button("Rechazar Spawn")){aiSpawnBatch_.reset();aiCommandBatchStatus_="AI SpawnBatch rechazado.";}
                }
                ImGui::SeparatorText("Effects AI");
                if(ImGui::Button("Cargar ai-effect-batch.json"))
                {
                    try{auto batch=WorldAiEffectBatch::Decode(ReadCandidateJson(std::filesystem::path("EditorCandidates")/"AI"/"ai-effect-batch.json"));if(batch.MapId!=activeMap_||batch.WorldNumber!=activeWorldNumber_)throw std::runtime_error("AI EffectBatch pertenece a otro mapa.");std::string error;if(!batch.Validate(effects_,error))throw std::runtime_error(error);aiEffectBatch_=std::move(batch);aiCommandBatchStatus_="AI EffectBatch validado; presets runtime preservados.";}
                    catch(const std::exception& error){aiEffectBatch_.reset();aiCommandBatchStatus_=error.what();}
                }
                if(aiEffectBatch_)
                {
                    ImGui::Text("%zu Lights | %zu Runtime Effects",aiEffectBatch_->Lights.size(),aiEffectBatch_->RuntimeEffects.size());
                    for(const auto& c:aiEffectBatch_->Lights)ImGui::BulletText("Light #%llu radius %d => %d | intensity %.2f => %.2f",static_cast<unsigned long long>(c.Id),c.Expected.Radius,c.Desired.Radius,c.Expected.Intensity,c.Desired.Intensity);
                    for(const auto& c:aiEffectBatch_->RuntimeEffects)ImGui::BulletText("Effect #%llu %s | scale %.2f => %.2f | enabled %s => %s",static_cast<unsigned long long>(c.Id),c.Expected.Preset.c_str(),c.Expected.Scale,c.Desired.Scale,c.Expected.Enabled?"ON":"OFF",c.Desired.Enabled?"ON":"OFF");
                    if(ImGui::Button("Aceptar Effect Batch")){std::string error;if(aiEffectBatch_->Apply(effects_,error)){aiCommandBatchStatus_="AI EffectBatch aplicado a Candidate; runtime/Data intactos hasta Save/Preview.";aiEffectBatch_.reset();}else aiCommandBatchStatus_=error;}
                    ImGui::SameLine();if(ImGui::Button("Rechazar Effects")){aiEffectBatch_.reset();aiCommandBatchStatus_="AI EffectBatch rechazado.";}
                }
                ImGui::SeparatorText("Terrain AI");
                ImGui::BeginDisabled(!terrainAvailable_);
                if(ImGui::Button("Cargar ai-terrain-batch.json"))
                {
                    try{auto batch=WorldAiTerrainBatch::Decode(ReadCandidateJson(std::filesystem::path("EditorCandidates")/"AI"/"ai-terrain-batch.json"));std::string error;if(!batch.Validate(terrainDocument_,activeMap_,error))throw std::runtime_error(error);aiTerrainBatch_=std::move(batch);aiCommandBatchStatus_="AI TerrainBatch validado; strokes semanticos pendientes de confirmacion.";}
                    catch(const std::exception& error){aiTerrainBatch_.reset();aiCommandBatchStatus_=error.what();}
                }
                ImGui::EndDisabled();
                if(aiTerrainBatch_)
                {
                    ImGui::Text("%zu Terrain strokes | expected revision %llu",aiTerrainBatch_->Strokes.size(),static_cast<unsigned long long>(aiTerrainBatch_->ExpectedRevision));
                    if(ImGui::BeginChild("ai-terrain-list",{0,150},ImGuiChildFlags_Borders)){for(const auto&s:aiTerrainBatch_->Strokes)ImGui::BulletText("Cell %d,%d | mode %d | radius %d | strength %d",s.X,s.Y,static_cast<int>(s.Brush.Mode),s.Brush.Radius,s.Brush.Strength);}ImGui::EndChild();
                    if(ImGui::Button("Aceptar Terrain Batch")){std::string error;if(aiTerrainBatch_->Apply(terrainDocument_,activeMap_,error)){aiCommandBatchStatus_="AI TerrainBatch aplicado mediante Terrain 2.0 strokes; Candidate/Undo disponibles.";aiTerrainBatch_.reset();}else aiCommandBatchStatus_=error;}
                    ImGui::SameLine();if(ImGui::Button("Rechazar Terrain")){aiTerrainBatch_.reset();aiCommandBatchStatus_="AI TerrainBatch rechazado.";}
                }
                if(!aiCommandBatchStatus_.empty())ImGui::TextWrapped("%s",aiCommandBatchStatus_.c_str());
            }
            ImGui::End();
            if(studioContract_&&!studioDiffs_.empty())
            {
                ImGui::SetNextWindowSize({560,420},ImGuiCond_FirstUseEver);
                if(ImGui::Begin("Studio / Blender Candidate"))
                {
                    ImGui::Text("%zu transformaciones propuestas | %s",studioDiffs_.size(),studioProposalApplied_?"APLICADAS AL DOCUMENTO CANDIDATE":"PENDIENTES");
                    ImGui::TextDisabled("Solo Position/Rotation/Scale. Type, indice y baseline permanecen bloqueados.");
                    if(ImGui::BeginChild("studio-diff-list",{0,250},ImGuiChildFlags_Borders))
                    {
                        for(const auto& diff:studioDiffs_)
                        {
                            ImGui::PushID(diff.Index);ImGui::Text("OBJ [%d] Type %d",diff.Index,diff.Before.Type);
                            if(diff.Before.Position!=diff.After.Position)ImGui::BulletText("Pos %.1f %.1f %.1f => %.1f %.1f %.1f",diff.Before.Position[0],diff.Before.Position[1],diff.Before.Position[2],diff.After.Position[0],diff.After.Position[1],diff.After.Position[2]);
                            if(diff.Before.Angle!=diff.After.Angle)ImGui::BulletText("Rot %.1f %.1f %.1f => %.1f %.1f %.1f",diff.Before.Angle[0],diff.Before.Angle[1],diff.Before.Angle[2],diff.After.Angle[0],diff.After.Angle[1],diff.After.Angle[2]);
                            if(diff.Before.Scale!=diff.After.Scale)ImGui::BulletText("Scale %.3f => %.3f",diff.Before.Scale,diff.After.Scale);
                            ImGui::Separator();ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                    ImGui::BeginDisabled(studioProposalApplied_);
                    if(ImGui::Button("Aceptar lote en Candidate"))
                    {
                        std::vector<WorldObjectUpdate> updates;updates.reserve(studioDiffs_.size());const auto& entries=document_.Entries();
                        for(const auto& diff:studioDiffs_)
                        {
                            if(diff.Index<0||static_cast<std::size_t>(diff.Index)>=entries.size()||!entries[static_cast<std::size_t>(diff.Index)].Record)throw std::runtime_error("Studio diff ya no coincide con el documento actual.");
                            const auto& live=*entries[static_cast<std::size_t>(diff.Index)].Record;
                            if(!(live==diff.Before))throw std::runtime_error("Studio diff bloqueado: objeto cambio desde que se cargo la propuesta.");
                            updates.push_back({entries[static_cast<std::size_t>(diff.Index)].Id,diff.After});
                        }
                        if(document_.SetMany(updates,"Apply Studio Transform Batch")){studioProposalApplied_=true;message_="Studio Candidate aplicado como una unica transaccion Undo/Redo. OBJ original intacto.";}
                    }
                    ImGui::EndDisabled();ImGui::SameLine();
                    if(ImGui::Button("Descartar propuesta")){studioDiffs_.clear();studioContract_.reset();message_="Propuesta Studio descartada; documento sin cambios adicionales.";}
                    ImGui::TextDisabled("Ctrl+Z revierte todo el lote aceptado de forma atomica.");
                }
                ImGui::End();
            }
            if (const auto* model = aiUi_.Draw(activeMap_, activeWorldNumber_,
                document_, scene_, ui_.Selected(), studioContract_ ? &*studioContract_ : nullptr))
            {
                ui_.ChoosePrototype(*model, document_);
                tool_ = activeTool_ = 0;
                message_ = "Silver AI sugirio un modelo validado. Vista fantasma activa; confirma manualmente sobre el mapa.";
            }
        }

        if (wasOptionsVisible && !optionsVisible) toolOptionsOpen_ = false;
        ui_.WorkspaceLayout().ResetPositions = false;
        if(tool_==5)npcLibraryUi_.Update(monsterSpawns_,scene_,selectedSpawn_);
        else npcLibraryUi_.Cancel();
        CaptureGlobalHistory();

        const auto& io = ImGui::GetIO();
        if (!io.WantTextInput && !io.WantCaptureMouse
            && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)
            && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt
            && std::isfinite(io.MouseWheel) && io.MouseWheel != 0.0f)
        {
            scene_.ZoomCamera(io.MouseWheel);
        }

        UpdateEffectPlacement();
        scene_.Synchronize(document_);
        if (terrainAvailable_) terrainScene_.Synchronize(terrainDocument_);
        if (save) Save();
    }
    catch (const std::exception& error)
    {
        message_ = error.what();
        ui_.SetStatus(message_);
    }
}
}
bool IsStandaloneMode() { return AuthorizedRuntime() && StandaloneRequested(); }
bool FreeLookEnabled() { return !IsStandaloneMode() || Session().FreeLookEnabled(); }
bool ViewportCameraMouseEnabled()
{
    if (!ImGui::GetCurrentContext()) return false;
    const auto& io = ImGui::GetIO();
    return !io.AppFocusLost && !io.WantCaptureMouse && !io.WantTextInput
        && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
}
bool ToggleFreeLookHotkey() { return Session().ToggleFreeLookHotkey(); }
bool ResetCameraHotkey() { return Session().ResetCameraHotkey(); }
void BootstrapStandalone() { Session().BootstrapStandalone(); }
void UpdateClientEditor() { Session().Update(); }
void DrawClientEditor(bool editorVisible) { Session().Draw(IsStandaloneMode() ? true : editorVisible); }
void RenderClientPlacementPreview() { Session().RenderPreview(); }
void ApplyRuntimeEffects() { Session().ApplyEffects(); }
bool CaptureGameInput() { return Session().CapturesInput(); }
void BeforeWorldChange() { Session().BeforeMapChange(); }
}
#endif
