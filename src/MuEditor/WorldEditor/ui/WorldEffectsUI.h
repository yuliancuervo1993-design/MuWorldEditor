#pragma once
#include "../core/WorldEffectDocument.h"
#include "IWorldEditorScene.h"
#include <string>

namespace Silver::WorldEditor
{
class WorldEffectsUI
{
public:
    bool Draw(WorldEffectDocument& document, IWorldEditorScene& scene, bool* open = nullptr, bool resetLayout = false);
    void Reset();
    EffectId Selected() const { return selected_; }
    void SelectById(const WorldEffectDocument& document, EffectId id) { Select(document.Find(id)); }
private:
    void Select(const WorldPointLight* light);

    EffectId selected_ = NoEffect;
    EffectId editingId_ = NoEffect;
    WorldPointLight editing_{};
    std::string status_;
};
}
