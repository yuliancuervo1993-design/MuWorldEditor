#pragma once
#include "IWorldEditorScene.h"
#include "WorldModelInspectionUI.h"
#include "../core/WorldModelCatalog.h"
#include "../core/WorldGlobalAssetRegistry.h"
#include "../core/WorldSilverHdRegistry.h"
#include "../core/WorldSilverHdPromotion.h"
#include "../core/WorldAiCandidateCatalog.h"
#include "../core/WorldModelPackage.h"
#include "../core/WorldBlenderProcess.h"
#include <future>
#include <chrono>
#include <map>
namespace Silver::WorldEditor
{
class WorldModelLibraryUI
{
public:
    const WorldPaletteItem* Draw(IWorldEditorScene& scene, bool* open = nullptr, bool resetLayout = false);
private:
    void Scan();
    void DrawModelGrid();
    const WorldPaletteItem* PlaceSelected(IWorldEditorScene& scene);
    bool quickPlace_ = false;
    std::map<std::size_t,WorldModelInspectionUI> thumbnails_;
    void ScanCandidates();
    void ScanPackages();
    void ScanSilverHd();
    void PollSilverHd();
    void Filter();
    void OpenInBlender(const WorldModelAsset& asset);
    void PollBlender();
    WorldBlenderProcess blenderProcess_;
    std::filesystem::path blenderWorkspace_;
    bool blenderImportReady_ = false;
    std::chrono::steady_clock::time_point blenderLastPoll_{};
    WorldGlobalAssetRegistry registry_;
    WorldSilverHdRegistry silverHdRegistry_;
    WorldModelInspectionUI inspection_;
    WorldModelInspectionUI silverHdInspection_;
    WorldAiCandidateCatalog candidateCatalog_;
    WorldModelInspectionUI candidateInspection_;
    WorldModelInspectionUI importedInspection_;
    bool attempted_ = false;
    bool candidateAttempted_ = false;
    bool packagesAttempted_ = false;
    bool silverHdAttempted_ = false;
    bool silverHdLoading_ = false;
    std::future<WorldSilverHdRegistry> silverHdFuture_;
    char search_[96]{};
    char terrainSearch_[64]{};
    char silverHdSearch_[96]{};
    int source_ = 0, category_ = 0, folder_ = 0;
    int terrainCategory_ = 0;
    int silverHdState_ = 0;
    void DrawSelectedTexture(IWorldEditorScene& scene, const WorldModelAsset& asset);
    int previewType_ = -1;
    std::uint64_t previewScene_ = 0;
    std::string previewPath_;
    int selected_ = -1;
    int terrainSelected_ = -1;
    int silverHdSelected_ = -1;
    int candidateSelected_ = -1;
    int inboxSelected_ = -1;
    int importedSelected_ = -1;
    std::vector<std::size_t> filtered_;
    std::vector<std::string> searchTexts_, folders_;
    std::vector<WorldModelPackageEntry> inboxPackages_;
    std::vector<WorldModelPackageEntry> importedPackages_;
    std::string error_;
    std::string transferStatus_;
    std::string silverHdError_;
    std::string silverHdPromotionStatus_;
    std::optional<WorldSilverHdPromotionPlan> silverHdPromotion_;
};
}
