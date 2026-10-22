#pragma once
#include "WorldEffectDocument.h"
#include "WorldObjectDocument.h"
#include "WorldMonsterSpawnDocument.h"
#include "WorldGateDocument.h"
#include "WorldRegionDocument.h"
#include "WorldTriggerDocument.h"
#include "WorldLayerDocument.h"
#include "WorldTerrainDocument.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
enum class WorldValidationSeverity { Info, Warning, Error };
enum class WorldValidationEntity { World, Object, Light, Terrain, MonsterSpawn, Gate, Region, Trigger };

struct WorldValidationIssue
{
    WorldValidationSeverity Severity = WorldValidationSeverity::Info;
    WorldValidationEntity Entity = WorldValidationEntity::World;
    std::uint64_t Id = 0;
    std::string Message;
};

struct WorldValidationReport
{
    std::vector<WorldValidationIssue> Issues;
    std::size_t Errors = 0;
    std::size_t Warnings = 0;
    bool Passed() const { return Errors == 0; }
};

class WorldMapValidator
{
public:
    static WorldValidationReport Validate(
        const WorldObjectDocument& objects,
        const WorldEffectDocument& effects,
        const WorldMonsterSpawnDocument* spawns = nullptr,
        const WorldGateDocument* gates = nullptr,
        const WorldRegionDocument* regions = nullptr,
        const WorldTriggerDocument* triggers = nullptr,
        const WorldLayerDocument* layers = nullptr,
        const WorldTerrainDocument* terrain = nullptr);
};
}
