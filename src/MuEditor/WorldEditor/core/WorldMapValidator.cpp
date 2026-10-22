#include "WorldMapValidator.h"
#include "TerrainAttributeCodec.h"
#include "WorldObjectRecord.h"
#include "WorldRuntimeEffectCatalog.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <tuple>

namespace Silver::WorldEditor
{
namespace
{
constexpr float CellWidth = 100.0f;
constexpr float MaximumWorldCoordinate = TerrainSide * CellWidth;

void Add(WorldValidationReport& report, WorldValidationSeverity severity,
    WorldValidationEntity entity, std::uint64_t id, std::string message)
{
    report.Issues.push_back({severity, entity, id, std::move(message)});
    if (severity == WorldValidationSeverity::Error) ++report.Errors;
    if (severity == WorldValidationSeverity::Warning) ++report.Warnings;
}

bool OutsideMap(const std::array<float, 3>& position)
{
    return position[0] < 0.0f || position[1] < 0.0f
        || position[0] >= MaximumWorldCoordinate || position[1] >= MaximumWorldCoordinate;
}
}

WorldValidationReport WorldMapValidator::Validate(
    const WorldObjectDocument& objects,
    const WorldEffectDocument& effects,
    const WorldMonsterSpawnDocument* spawns,
    const WorldGateDocument* gates,
    const WorldRegionDocument* regions,
    const WorldTriggerDocument* triggers,
    const WorldLayerDocument* layers,
    const WorldTerrainDocument* terrain)
{
    WorldValidationReport report;
    std::map<std::tuple<std::int16_t, int, int, int>, ObjectId> placements;

    for (const auto& entry : objects.Entries())
    {
        if (!entry.Record || entry.Record->BaselineOnly) continue;
        const auto& record = *entry.Record;
        try { ValidateRecord(record); }
        catch (const std::exception& error)
        {
            Add(report, WorldValidationSeverity::Error, WorldValidationEntity::Object,
                entry.Id, "Registro invalido: " + std::string(error.what()));
            continue;
        }

        if (OutsideMap(record.Position))
            Add(report, WorldValidationSeverity::Warning, WorldValidationEntity::Object,
                entry.Id, "Objeto fuera de los limites XY del terreno 256x256.");

        const auto key = std::make_tuple(record.Type,
            static_cast<int>(std::lround(record.Position[0] * 10.0f)),
            static_cast<int>(std::lround(record.Position[1] * 10.0f)),
            static_cast<int>(std::lround(record.Position[2] * 10.0f)));
        if (const auto found = placements.find(key); found != placements.end())
            Add(report, WorldValidationSeverity::Warning, WorldValidationEntity::Object,
                entry.Id, "Posible duplicado exacto de objeto #" + std::to_string(found->second) + ".");
        else
            placements.emplace(key, entry.Id);
    }

    for (const auto& light : effects.Lights())
    {
        const bool finitePosition = std::all_of(light.Position.begin(), light.Position.end(),
            [](float value) { return std::isfinite(value); });
        const bool finiteColor = std::all_of(light.Color.begin(), light.Color.end(),
            [](float value) { return std::isfinite(value); });
        if (!finitePosition || !finiteColor || !std::isfinite(light.Intensity)
            || light.Intensity < 0.0f || light.Intensity > 4.0f
            || light.Radius < 1 || light.Radius > 32)
        {
            Add(report, WorldValidationSeverity::Error, WorldValidationEntity::Light,
                light.Id, "Propiedades de Point Light fuera de rango.");
            continue;
        }
        if (OutsideMap(light.Position))
            Add(report, WorldValidationSeverity::Warning, WorldValidationEntity::Light,
                light.Id, "Point Light fuera de los limites XY del terreno 256x256.");
    }

    const auto& verifiedEffects=WorldRuntimeEffectCatalog::Presets();
    for (const auto& effect : effects.RuntimeEffects())
    {
        const bool finitePosition=std::all_of(effect.Position.begin(),effect.Position.end(),[](float value){return std::isfinite(value);});
        if(!finitePosition||effect.Preset.empty()||effect.Kind<0||effect.Kind>1||effect.Type<0||effect.SubType<0||effect.SubType>255||!std::isfinite(effect.Scale)||effect.Scale<=0.0f||effect.Scale>10.0f)
        {
            Add(report,WorldValidationSeverity::Error,WorldValidationEntity::Light,effect.Id,"Runtime Effect contiene propiedades invalidas.");
            continue;
        }
        const auto verified=std::find_if(verifiedEffects.begin(),verifiedEffects.end(),[&](const auto& preset){return preset.Name==effect.Preset&&static_cast<int>(preset.Kind)==effect.Kind&&preset.Type==effect.Type&&preset.SubType==effect.SubType;});
        if(verified==verifiedEffects.end())Add(report,WorldValidationSeverity::Error,WorldValidationEntity::Light,effect.Id,"Runtime Effect ya no coincide con un preset verificado del cliente.");
        if(OutsideMap(effect.Position))Add(report,WorldValidationSeverity::Warning,WorldValidationEntity::Light,effect.Id,"Runtime Effect fuera de los limites XY del terreno 256x256.");
    }

    if (spawns && spawns->Loaded())
    {
        std::map<std::string,SpawnId> persistentIds;
        for (const auto& spawn : spawns->Entries())
        {
            if(spawn.PersistentId.empty())Add(report,WorldValidationSeverity::Error,WorldValidationEntity::MonsterSpawn,spawn.Id,"MonsterSpawn sin PersistentId OpenMU.");
            else if(const auto found=persistentIds.find(spawn.PersistentId);found!=persistentIds.end())Add(report,WorldValidationSeverity::Error,WorldValidationEntity::MonsterSpawn,spawn.Id,"PersistentId OpenMU duplicado con Spawn #"+std::to_string(found->second)+".");
            else persistentIds.emplace(spawn.PersistentId,spawn.Id);
            if(spawn.MonsterNumber<0||spawn.ObjectKind.empty())Add(report,WorldValidationSeverity::Error,WorldValidationEntity::MonsterSpawn,spawn.Id,"MonsterSpawn sin identidad MonsterDefinition/ObjectKind valida.");
            if(spawn.X1>spawn.X2||spawn.Y1>spawn.Y2)Add(report,WorldValidationSeverity::Error,WorldValidationEntity::MonsterSpawn,spawn.Id,"MonsterSpawn tiene area invertida.");
            if (spawn.Quantity == 0) Add(report, WorldValidationSeverity::Warning, WorldValidationEntity::MonsterSpawn,spawn.Id, "MonsterSpawn con Quantity 0.");
            if(spawn.Quantity<0)Add(report,WorldValidationSeverity::Error,WorldValidationEntity::MonsterSpawn,spawn.Id,"MonsterSpawn con Quantity negativa.");
            if(spawn.Direction>8)Add(report,WorldValidationSeverity::Warning,WorldValidationEntity::MonsterSpawn,spawn.Id,"MonsterSpawn Direction fuera del rango cardinal OpenMU 0..8.");
        }
    }

    if (gates && gates->Loaded())
    {
        for (const auto& gate : gates->Entries())
        {
            if (gate.TargetMap < 0 || gate.TargetGateId == NoGate)
                Add(report, WorldValidationSeverity::Error, WorldValidationEntity::Gate,
                    gate.Id, "Gate sin destino valido.");
        }
    }

    if (regions)
    {
        for (const auto& region : regions->Entries())
        {
            if (region.Name.empty())
                Add(report, WorldValidationSeverity::Error, WorldValidationEntity::Region,
                    region.Id, "Region sin nombre.");
            if (region.Shape == WorldRegionShape::Rectangle)
            {
                if (region.X1 < 0.0f || region.Y1 < 0.0f || region.X2 >= MaximumWorldCoordinate
                    || region.Y2 >= MaximumWorldCoordinate)
                    Add(report, WorldValidationSeverity::Warning, WorldValidationEntity::Region,
                        region.Id, "Region rectangular fuera de los limites del mapa.");
            }
            else
            {
                if (region.X1 - region.Radius < 0.0f || region.Y1 - region.Radius < 0.0f
                    || region.X1 + region.Radius >= MaximumWorldCoordinate
                    || region.Y1 + region.Radius >= MaximumWorldCoordinate)
                    Add(report, WorldValidationSeverity::Warning, WorldValidationEntity::Region,
                        region.Id, "Region circular fuera de los limites del mapa.");
            }
        }
    }

    if (triggers)
    {
        for (const auto& trigger : triggers->Entries())
        {
            if (!trigger.Enabled) continue;
            if (trigger.Region != NoRegion && (!regions || !regions->Find(trigger.Region)))
                Add(report, WorldValidationSeverity::Error, WorldValidationEntity::Trigger,
                    trigger.Id, "Trigger referencia una Region inexistente.");
            if(trigger.Name.empty()||trigger.Event.empty())Add(report,WorldValidationSeverity::Error,WorldValidationEntity::Trigger,trigger.Id,"Trigger habilitado sin Name/Event.");
            if (trigger.Actions.empty())
                Add(report, WorldValidationSeverity::Warning, WorldValidationEntity::Trigger,
                    trigger.Id, "Trigger habilitado sin Actions.");
            if(trigger.Conditions.size()>64||trigger.Actions.size()>64)Add(report,WorldValidationSeverity::Warning,WorldValidationEntity::Trigger,trigger.Id,"Trigger contiene mas de 64 Conditions/Actions; revisar complejidad.");
        }
    }

    if(layers)
    {
        std::map<std::string,WorldLayerId> names;
        for(const auto& layer:layers->Layers())
        {
            if(layer.Id==NoWorldLayer||layer.Name.empty())Add(report,WorldValidationSeverity::Error,WorldValidationEntity::World,layer.Id,"Layer sin ID/nombre valido.");
            const auto [it,inserted]=names.emplace(layer.Name,layer.Id);if(!inserted)Add(report,WorldValidationSeverity::Error,WorldValidationEntity::World,layer.Id,"Nombre de Layer duplicado con Layer #"+std::to_string(it->second)+".");
        }
        for(const auto& entry:objects.Entries())if(entry.Record)
        {
            const auto layerId=layers->LayerFor(entry.Id);if(layerId==NoWorldLayer||!layers->Find(layerId))Add(report,WorldValidationSeverity::Error,WorldValidationEntity::Object,entry.Id,"Objeto asignado a Layer inexistente.");
        }
    }

    if(terrain)
    {
        if(terrain->Heights().size()!=TerrainCells||terrain->Attributes().Cells.size()!=TerrainCells)Add(report,WorldValidationSeverity::Error,WorldValidationEntity::Terrain,0,"Terrain no contiene exactamente 65.536 alturas/atributos.");
        if(const auto& mapping=terrain->Mapping())
        {
            if(mapping->Layer1.size()!=TerrainCells||mapping->Layer2.size()!=TerrainCells||mapping->Alpha.size()!=TerrainCells)Add(report,WorldValidationSeverity::Error,WorldValidationEntity::Terrain,0,"Terrain mapping no contiene exactamente 65.536 celdas por capa.");
            else
            {
                std::size_t invalidBlend=0;
                for(std::size_t i=0;i<TerrainCells;++i)if(mapping->Layer2[i]==255&&mapping->Alpha[i]!=0)++invalidBlend;
                if(invalidBlend)Add(report,WorldValidationSeverity::Warning,WorldValidationEntity::Terrain,0,std::to_string(invalidBlend)+" celdas tienen Layer2=255 pero Alpha distinto de 0.");
            }
        }
    }

    if (report.Issues.empty())
        Add(report, WorldValidationSeverity::Info, WorldValidationEntity::World, 0,
            "Check Map: sin problemas detectados por los adapters actuales.");
    return report;
}
}
