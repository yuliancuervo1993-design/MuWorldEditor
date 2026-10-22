#include "WorldAiRequest.h"
#include "WorldModelCatalog.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include "json.hpp"

namespace Silver::WorldEditor
{
namespace
{
using Json = nlohmann::json;

std::string RequestId()
{
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "world-editor-ai-" + std::to_string(now);
}

std::string SafeBmdPath(int worldNumber, const std::optional<std::string>& model)
{
    if (!model || worldNumber < 1) return {};
    const std::filesystem::path value(*model);
    if (value.is_absolute() || value.has_parent_path() || value.filename() != value
        || value.string().find(':') != std::string::npos || value.string().find("..") != std::string::npos)
        return {};
    auto filename = value.filename().string();
    if (WorldModelCatalog::Lower(std::filesystem::path(filename).extension().string()) != ".bmd")
        filename += ".bmd";
    return (std::filesystem::path("Data") / ("Object" + std::to_string(worldNumber)) / filename).generic_string();
}

const SilverWorldStudioPlacement* PlacementForType(const SilverWorldStudioContract* bridge, int type)
{
    if (!bridge) return nullptr;
    const auto found = std::find_if(bridge->Placements.begin(), bridge->Placements.end(),
        [type](const auto& placement) { return placement.Type == type; });
    return found == bridge->Placements.end() ? nullptr : &*found;
}

int InstancesForType(const SilverWorldStudioContract* bridge, int type)
{
    if (!bridge) return 1;
    const auto* first = PlacementForType(bridge, type);
    if (first && first->InstancesOfType && *first->InstancesOfType > 0)
        return *first->InstancesOfType;
    const auto count = std::count_if(bridge->Placements.begin(), bridge->Placements.end(),
        [type](const auto& placement) { return placement.Type == type; });
    return std::max(1, static_cast<int>(count));
}

void AddReferences(WorldAiRequest& result, const SilverWorldStudioContract* bridge, int type)
{
    std::set<std::string> unique;
    if (const auto* placement = PlacementForType(bridge, type); placement && placement->Model)
        unique.insert(*placement->Model);
    if (bridge)
    {
        for (const auto& placement : bridge->Placements)
        {
            if (!placement.Model || !unique.insert(*placement.Model).second) continue;
            if (unique.size() >= 24) break;
        }
    }
    result.VerifiedReferences.assign(unique.begin(), unique.end());
}

WorldAiRequest Base(int mapId, int worldNumber, std::string mapName,
    std::array<float, 3> position, const SilverWorldStudioContract* bridge,
    WorldAiWorkMode mode, std::string prompt)
{
    WorldAiRequest result;
    result.RequestId = RequestId();
    result.WorkMode = mode;
    result.MapId = mapId;
    result.WorldNumber = worldNumber;
    result.MapName = std::move(mapName);
    result.Position = position;
    result.Prompt = std::move(prompt);
    result.ExpectedPlacements = 1;
    if (bridge)
    {
        if (result.MapName.empty()) result.MapName = bridge->Map;
        result.ExpectedPlacements = 1;
    }
    return result;
}

void CompleteFromType(WorldAiRequest& result, int type, const SilverWorldStudioContract* bridge)
{
    if (type < 0 || type > std::numeric_limits<std::int16_t>::max()) return;
    result.Type = static_cast<std::int16_t>(type);
    result.ModelSequence = type + 1;
    result.ExpectedPlacements = InstancesForType(bridge, type);
    if (const auto* placement = PlacementForType(bridge, type))
        result.ResolvedBmd = SafeBmdPath(result.WorldNumber, placement->Model);
    AddReferences(result, bridge, type);
}
}

std::string WorldAiRequest::ModeName(WorldAiWorkMode mode)
{
    switch (mode)
    {
    case WorldAiWorkMode::ImproveExisting: return "IMPROVE_EXISTING";
    case WorldAiWorkMode::CreateNew: return "CREATE_NEW";
    case WorldAiWorkMode::ReferenceGuided: return "REFERENCE_GUIDED";
    }
    return "IMPROVE_EXISTING";
}

WorldAiRequest WorldAiRequest::FromSelection(int mapId, int worldNumber,
    const WorldObjectDocument& document, ObjectId selected,
    const SilverWorldStudioContract* bridge, WorldAiWorkMode mode, std::string prompt)
{
    if (selected == NoObject) throw std::invalid_argument("No hay un objeto seleccionado para Silver AI.");
    const auto* record = document.Find(selected);
    if (!record || record->BaselineOnly)
        throw std::invalid_argument("El objeto seleccionado no es editable por el World Editor.");

    std::string mapName = bridge ? bridge->Map : "Map" + std::to_string(mapId);
    auto result = Base(mapId, worldNumber, std::move(mapName), record->Position, bridge, mode, std::move(prompt));
    result.SelectedObject = selected;
    result.Rotation = record->Angle;
    result.Scale = record->Scale;
    CompleteFromType(result, record->Type, bridge);
    return result;
}

WorldAiRequest WorldAiRequest::ForPlacement(int mapId, int worldNumber, std::string mapName,
    std::array<float, 3> position, const SilverWorldStudioContract* bridge,
    WorldAiWorkMode mode, std::string prompt)
{
    auto result = Base(mapId, worldNumber, std::move(mapName), position, bridge, mode, std::move(prompt));
    return result;
}

std::string WorldAiRequest::ToJson() const
{
    Json references = Json::array();
    for (const auto& reference : VerifiedReferences) references.push_back(reference);
    Json result{
        {"schema", SilverWorldEditorAiRequestSchema},
        {"requestId", RequestId},
        {"workMode", ModeName(WorkMode)},
        {"mapId", MapId}, {"worldNumber", WorldNumber}, {"map", MapName},
        {"selectedObject", SelectedObject == NoObject ? Json(nullptr) : Json(SelectedObject)},
        {"type", Type < 0 ? Json(nullptr) : Json(Type)},
        {"modelSequence", ModelSequence < 0 ? Json(nullptr) : Json(ModelSequence)},
        {"position", Position}, {"rotation", Rotation}, {"scale", Scale},
        {"resolvedBmd", ResolvedBmd ? Json(*ResolvedBmd) : Json(nullptr)},
        {"expectedPlacements", ExpectedPlacements}, {"verifiedReferences", references},
        {"prompt", Prompt}, {"safety", Safety},
        {"previewOnly", true}, {"autoApply", false}
    };
    return result.dump(2) + "\n";
}
}
