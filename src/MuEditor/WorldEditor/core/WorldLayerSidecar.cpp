#include "WorldLayerSidecar.h"
#include "json.hpp"
#include <algorithm>
#include <bit>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Silver::WorldEditor
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t MaximumSidecarBytes = 1024 * 1024;
constexpr std::size_t MaximumMetadataAssignments = MaximumObjects;

std::unordered_map<std::string, std::vector<ObjectId>> BuildKeyIndex(const WorldObjectFile& baseline)
{
    std::unordered_map<std::string, std::vector<ObjectId>> result;
    result.reserve(baseline.Records.size());
    for (std::size_t index = 0; index < baseline.Records.size(); ++index)
    {
        const auto& record = baseline.Records[index];
        if (record.BaselineOnly) continue;
        result[WorldLayerSidecar::ObjectKey(record)].push_back(static_cast<ObjectId>(index + 1));
    }
    return result;
}
}

std::string WorldLayerSidecar::ObjectKey(const WorldObjectRecord& record)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
        << 'T' << std::setw(4) << static_cast<std::uint16_t>(record.Type);
    const auto appendFloat = [&stream](float value)
    {
        stream << '-' << std::setw(8) << std::bit_cast<std::uint32_t>(value);
    };
    for (float value : record.Position) appendFloat(value);
    for (float value : record.Angle) appendFloat(value);
    appendFloat(record.Scale);
    return stream.str();
}

WorldLayerSidecarExport WorldLayerSidecar::Encode(
    const WorldObjectFile& baseline,
    const WorldLayerDocument& layers,
    int mapId,
    int worldNumber)
{
    const auto index = BuildKeyIndex(baseline);

    Json layerValues = Json::array();
    for (const auto& layer : layers.Layers())
    {
        layerValues.push_back({
            {"id", layer.Id},
            {"name", layer.Name},
            {"visible", layer.Visible},
            {"locked", layer.Locked}
        });
    }

    WorldLayerSidecarExport result;
    Json assignments = Json::array();
    for (std::size_t recordIndex = 0; recordIndex < baseline.Records.size(); ++recordIndex)
    {
        const auto& record = baseline.Records[recordIndex];
        if (record.BaselineOnly) continue;

        const ObjectId objectId = static_cast<ObjectId>(recordIndex + 1);
        const WorldLayerId layerId = layers.LayerFor(objectId);
        if (layerId == DefaultWorldLayer) continue;

        const std::string key = ObjectKey(record);
        const auto found = index.find(key);
        if (found == index.end() || found->second.size() != 1)
        {
            ++result.AmbiguousAssignmentsSkipped;
            continue;
        }

        assignments.push_back({{"key", key}, {"layerId", layerId}});
        ++result.PersistentAssignments;
    }

    Json json{
        {"schema", Schema},
        {"mapId", mapId},
        {"worldNumber", worldNumber},
        {"baselineObjectCount", baseline.Records.size()},
        {"layers", layerValues},
        {"assignments", assignments},
        {"ambiguousAssignmentsSkipped", result.AmbiguousAssignmentsSkipped}
    };

    const std::string text = json.dump(2) + "\n";
    if (text.size() > MaximumSidecarBytes)
        throw std::length_error("El sidecar de Layers excede el limite permitido.");
    result.Bytes.assign(text.begin(), text.end());
    return result;
}

WorldLayerSidecarRestore WorldLayerSidecar::Decode(
    std::span<const std::uint8_t> bytes,
    const WorldObjectFile& baseline,
    int mapId,
    int worldNumber)
{
    if (bytes.empty() || bytes.size() > MaximumSidecarBytes)
        throw std::invalid_argument("Sidecar de Layers fuera de limites.");

    Json json;
    try
    {
        json = Json::parse(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
    catch (const Json::exception&)
    {
        throw std::invalid_argument("Sidecar de Layers no contiene JSON valido.");
    }

    if (!json.is_object() || json.value("schema", "") != Schema)
        throw std::invalid_argument("Schema de Layers no compatible.");
    if (json.value("mapId", -1) != mapId || json.value("worldNumber", -1) != worldNumber)
        throw std::invalid_argument("El sidecar de Layers pertenece a otro mapa.");
    if (json.value("baselineObjectCount", static_cast<std::size_t>(-1)) != baseline.Records.size())
        throw std::invalid_argument("El baseline del mapa cambio; Layers no se restauran.");
    if (!json.contains("layers") || !json.at("layers").is_array()
        || json.at("layers").empty() || json.at("layers").size() > 64)
        throw std::invalid_argument("Lista de Layers invalida.");
    if (!json.contains("assignments") || !json.at("assignments").is_array()
        || json.at("assignments").size() > MaximumMetadataAssignments)
        throw std::invalid_argument("Asignaciones de Layers invalidas.");

    WorldLayerSidecarRestore result;
    std::unordered_map<WorldLayerId, WorldLayerId> layerIds;
    std::unordered_set<WorldLayerId> savedIds;

    bool baseSeen = false;
    for (const auto& value : json.at("layers"))
    {
        if (!value.is_object()) throw std::invalid_argument("Layer invalida.");
        const auto savedId = value.value("id", static_cast<WorldLayerId>(NoWorldLayer));
        const std::string name = value.value("name", std::string{});
        const bool visible = value.value("visible", true);
        const bool locked = value.value("locked", false);
        if (savedId == NoWorldLayer || !savedIds.insert(savedId).second)
            throw std::invalid_argument("ID de Layer invalido o duplicado.");

        WorldLayerId currentId = DefaultWorldLayer;
        if (savedId == DefaultWorldLayer)
        {
            if (baseSeen) throw std::invalid_argument("Layer Base duplicada.");
            baseSeen = true;
            result.Layers.Rename(DefaultWorldLayer, name);
        }
        else
        {
            currentId = result.Layers.Add(name);
        }
        result.Layers.SetVisible(currentId, visible);
        result.Layers.SetLocked(currentId, locked);
        layerIds.emplace(savedId, currentId);
    }
    if (!baseSeen) throw std::invalid_argument("Falta la Layer Base.");

    const auto keyIndex = BuildKeyIndex(baseline);
    std::unordered_set<std::string> metadataKeys;
    for (const auto& value : json.at("assignments"))
    {
        if (!value.is_object()) throw std::invalid_argument("Asignacion de Layer invalida.");
        const std::string key = value.value("key", std::string{});
        const auto savedLayer = value.value("layerId", static_cast<WorldLayerId>(NoWorldLayer));
        if (key.empty() || !metadataKeys.insert(key).second)
            throw std::invalid_argument("Clave de objeto vacia o duplicada en Layers.");
        const auto layer = layerIds.find(savedLayer);
        if (layer == layerIds.end())
            throw std::invalid_argument("Asignacion referencia una Layer inexistente.");

        const auto object = keyIndex.find(key);
        if (object == keyIndex.end())
        {
            ++result.MissingAssignmentsSkipped;
            continue;
        }
        if (object->second.size() != 1)
        {
            ++result.AmbiguousAssignmentsSkipped;
            continue;
        }

        result.Layers.Assign(object->second.front(), layer->second);
        ++result.RestoredAssignments;
    }
    return result;
}
}
