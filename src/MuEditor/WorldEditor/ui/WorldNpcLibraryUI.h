#pragma once
#include "IWorldEditorScene.h"
#include "WorldModelInspectionUI.h"
#include "../core/WorldNpcCatalog.h"
namespace Silver::WorldEditor
{
class WorldNpcLibraryUI
{
public:
    void Draw(WorldMonsterSpawnDocument& document, bool* open);
    void Update(WorldMonsterSpawnDocument& document,IWorldEditorScene& scene,SpawnId& selected);
    void Choose(const WorldMonsterSpawn& prototype) { prototype_=prototype; placing_=true; placementScene_.reset(); }
    void SetRepeat(bool value) { repeat_=value; }
    bool Placing() const { return placing_; }
    void Cancel() { placing_=false; }
private:
    void Reload();
    void BeginPlacement(const WorldMonsterSpawnDocument& document);
    void DrawSelection();
    void DrawCatalog();
    void Filter();
    WorldNpcCatalog catalog_;
    WorldMonsterSpawn prototype_;
    bool loaded_=false,placing_=false,repeat_=false;
    std::optional<std::uint64_t> placementScene_;
    std::vector<std::size_t> filtered_;
    WorldModelInspectionUI inspection_;
    int previewNumber_=-1;
    int selected_=-1,kind_=0,quantity_=1,direction_=0,variant_=0;
    char search_[96]{};
    std::string status_;
};
}
