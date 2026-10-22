#pragma once
#ifdef _EDITOR
#include "../core/TerrainCandidateStore.h"
#include "../ui/TerrainBrushUI.h"
#include <memory>
namespace Silver::WorldEditor
{
class MuTerrainTools
{
public:
    void Open();
    void Draw(IWorldEditorScene& scene);
    void Cancel();
    void BeforeMapChange();
    bool Dirty() const { return document_.Dirty(); }
    bool Drawing() const { return document_.Drawing(); }
    bool Loaded() const { return static_cast<bool>(store_); }
    void Save();
private:
    void Synchronize(bool force=false);
    TerrainBrushDocument document_;
    TerrainBrushUI ui_;
    std::unique_ptr<TerrainCandidateStore> store_;
    std::vector<std::uint8_t> appliedHeights_;
    std::vector<std::uint16_t> appliedAttributes_;
    std::uint64_t revision_=0;
    double lastSync_=0;
    bool attached_=false;
};
}
#endif
