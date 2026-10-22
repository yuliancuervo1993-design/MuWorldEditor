#pragma once
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
enum class WorldRuntimeEffectKind { Particle, Effect };
struct WorldRuntimeEffectPreset { std::string Name; WorldRuntimeEffectKind Kind; int Type; int SubType; float Scale; std::string Evidence; };
class WorldRuntimeEffectCatalog
{
public:
    static const std::vector<WorldRuntimeEffectPreset>& Presets();
};
}
