#include "WorldRuntimeEffectCatalog.h"
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include "Core/Platform/WinCompat.h"
#endif
#include "Core/Globals/_enum.h"
#include "Core/Globals/_TextureIndex.h"
namespace Silver::WorldEditor
{
const std::vector<WorldRuntimeEffectPreset>& WorldRuntimeEffectCatalog::Presets()
{
    static const std::vector<WorldRuntimeEffectPreset> presets{
        {"Fire",WorldRuntimeEffectKind::Particle,BITMAP_FIRE,0,1.0f,"Render/Effects EffectRegistry + CreateParticle runtime"},
        {"True Fire",WorldRuntimeEffectKind::Particle,BITMAP_TRUE_FIRE,0,1.0f,"GameMaps BattleCastle/NewTown/SwampOfQuiet"},
        {"Smoke",WorldRuntimeEffectKind::Particle,BITMAP_SMOKE,3,1.0f,"GameMaps Raklion/DoppelGanger"},
        {"Waterfall 1",WorldRuntimeEffectKind::Particle,BITMAP_WATERFALL_1,2,1.0f,"GameMaps Aida"},
        {"Waterfall 2",WorldRuntimeEffectKind::Particle,BITMAP_WATERFALL_2,6,1.0f,"GameMaps PK Field"},
        {"Waterfall 3",WorldRuntimeEffectKind::Particle,BITMAP_WATERFALL_3,3,1.0f,"GameMaps HuntingGround"},
        {"Waterfall 5",WorldRuntimeEffectKind::Particle,BITMAP_WATERFALL_5,0,1.0f,"GameMaps UnitedMarketPlace"},
        {"Bubble",WorldRuntimeEffectKind::Particle,BITMAP_BUBBLE,5,1.0f,"GameMaps Kanturu/DoppelGanger"},
        {"Warp",WorldRuntimeEffectKind::Effect,MODEL_WARP,1,1.0f,"GameMaps Raklion"},
        {"Fire Red",WorldRuntimeEffectKind::Effect,BITMAP_FIRE_RED,0,1.0f,"EffectRegistry descriptor"},
    };return presets;
}
}
